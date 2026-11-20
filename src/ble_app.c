/**
 * @file    ble_app.c
 * @brief   本工程自己的最小 BLE 从机实现
 *
 * 与 SDK demo（vendor/acl_peripheral_demo）的区别：
 *   · 只初始化「从机 + 可连接广播」，不初始化主机角色、OTA、HID
 *   · ATT 表只有两个服务：必需的 GAP + 一个自定义"对时"服务（UUID 沿用 Telink SPP，
 *     所以老的 tools/set_time.py 和手机 nRF Connect 的步骤都不用改）
 *   · 不做配对（特征权限不含加密位），手机连上就能直接写
 *   · 栈缓冲（ACL RX/TX FIFO、L2CAP MTU）由本文件自己定义，不再用 demo 的 app_buffer.c
 */
#include "tl_common.h"
#include "drivers.h"
#include "stack/ble/ble.h"

#include "app_cfg.h"
#include "ble_app.h"
#include "app.h"

/* ══════════════════════════════════════════════════════════════════════════
 *  栈缓冲：SDK 不做动态分配，这些数组必须由应用提供
 * ══════════════════════════════════════════════════════════════════════════ */
#define ACL_RX_FIFO_SIZE         CAL_LL_ACL_RX_FIFO_SIZE(ACL_CONN_MAX_RX_OCTETS)
#define ACL_SLAVE_TX_FIFO_SIZE   CAL_LL_ACL_TX_FIFO_SIZE(ACL_SLAVE_MAX_TX_OCTETS)
#define MTU_S_BUFF_SIZE          CAL_MTU_BUFF_SIZE(ATT_MTU_SLAVE_RX_MAX_SIZE)

_attribute_data_no_init_        u8 app_acl_rxfifo[ACL_RX_FIFO_SIZE * ACL_RX_FIFO_NUM] = {0};
_attribute_ble_data_retention_  u8 app_acl_slvTxfifo[ACL_SLAVE_TX_FIFO_SIZE * ACL_SLAVE_TX_FIFO_NUM * ACL_PERIPHR_MAX_NUM] = {0};
_attribute_ble_data_retention_  u8 mtu_s_rx_fifo[ACL_PERIPHR_MAX_NUM * MTU_S_BUFF_SIZE];
_attribute_ble_data_retention_  u8 mtu_s_tx_fifo[ACL_PERIPHR_MAX_NUM * MTU_S_BUFF_SIZE];

/* ══════════════════════════════════════════════════════════════════════════
 *  广播数据
 * ══════════════════════════════════════════════════════════════════════════ */
/* 广播名 / 设备名：12 个字符，写成字符列表（宏字符串不能直接塞进数组初值）*/
#define ADV_NAME_CHARS  'c','a','l','e','n','d','a','r','_','t','a','g'
#define ADV_NAME_LEN    12

static const u8 s_adv_data[] = {
    2,  DT_FLAGS,               0x06,   /* LE General Discoverable + 不支持 BR/EDR */
    ADV_NAME_LEN + 1, DT_COMPLETE_LOCAL_NAME, ADV_NAME_CHARS
};

static const u8 s_scan_rsp[] = {
    ADV_NAME_LEN + 1, DT_COMPLETE_LOCAL_NAME, ADV_NAME_CHARS
};

/* ══════════════════════════════════════════════════════════════════════════
 *  ATT 表
 *
 *  句柄 = 表内下标 + 1，所以表里的顺序就是句柄顺序。
 *  只保留必需的两块：
 *    ① GAP 服务（设备名 / appearance / 连接参数，手机 GAP 层要看）
 *    ② 对时服务（一个 write-without-response 特征，UUID 沿用 Telink SPP C2S）
 * ══════════════════════════════════════════════════════════════════════════ */
enum {
    ATT_H_START = 0,
    /* GAP */
    GA_PS_H, GA_NAME_CD_H, GA_NAME_DP_H, GA_APPEAR_CD_H, GA_APPEAR_DP_H, GA_CONN_CD_H, GA_CONN_DP_H,
    /* 对时服务 */
    TS_PS_H, TS_DATA_CD_H, TS_DATA_DP_H, TS_DATA_DESC_H,
    ATT_END_H,
};

typedef struct {
    u16 intervalMin;
    u16 intervalMax;
    u16 latency;
    u16 timeout;
} gap_peri_conn_params_t;

/* 属性 UUID（16 位）/ 值 */
static const u16 my_primaryServiceUUID = GATT_UUID_PRIMARY_SERVICE;       /* 0x2800 */
static const u16 my_characterUUID      = GATT_UUID_CHARACTER;             /* 0x2803 */
static const u16 my_gapServiceUUID     = SERVICE_UUID_GENERIC_ACCESS;     /* 0x1800 */
static const u16 my_devNameUUID        = GATT_UUID_DEVICE_NAME;           /* 0x2A00 */
static const u16 my_appearanceUIID     = GATT_UUID_APPEARANCE;            /* 0x2A01 */
static const u16 my_periConnParamUUID  = GATT_UUID_PERI_CONN_PARAM;       /* 0x2A04 */
static const u16 userdesc_UUID         = GATT_UUID_CHAR_USER_DESC;        /* 0x2901 */

/* 服务 UUID（128 位）——沿用 Telink SPP，手机/PC 端的既有脚本可以直接继续用 */
static const u8 tsServiceUUID[16] = WRAPPING_BRACES(TELINK_SPP_UUID_SERVICE);
static const u8 tsDataUUID[16]    = WRAPPING_BRACES(TELINK_SPP_DATA_CLIENT2SERVER);

/* GAP 各特征的声明值（属性 + 句柄 + UUID） */
static const u8 my_devNameCharVal[5] = {
    CHAR_PROP_READ,
    U16_LO(GA_NAME_DP_H), U16_HI(GA_NAME_DP_H),
    U16_LO(GATT_UUID_DEVICE_NAME), U16_HI(GATT_UUID_DEVICE_NAME)
};
static const u8 my_appearanceCharVal[5] = {
    CHAR_PROP_READ,
    U16_LO(GA_APPEAR_DP_H), U16_HI(GA_APPEAR_DP_H),
    U16_LO(GATT_UUID_APPEARANCE), U16_HI(GATT_UUID_APPEARANCE)
};
static const u8 my_periConnParamCharVal[5] = {
    CHAR_PROP_READ,
    U16_LO(GA_CONN_DP_H), U16_HI(GA_CONN_DP_H),
    U16_LO(GATT_UUID_PERI_CONN_PARAM), U16_HI(GATT_UUID_PERI_CONN_PARAM)
};

/* 对时特征的声明值（19 字节：属性 1 + 句柄 2 + 128 位 UUID 16）*/
static const u8 tsDataCharVal[19] = {
    CHAR_PROP_READ | CHAR_PROP_WRITE_WITHOUT_RSP,
    U16_LO(TS_DATA_DP_H), U16_HI(TS_DATA_DP_H),
    TELINK_SPP_DATA_CLIENT2SERVER
};

static const u8 my_devName[] = {ADV_NAME_CHARS};              /* 与广播名一致 */
static const u16 my_appearance = GAP_APPEARE_UNKNOWN;
static const gap_peri_conn_params_t my_periConnParameters = {20, 40, 0, 400};
static const u8 tsDataDesc[] = "time set";

/* 特征值的落地缓冲（1 字节够用，真正数据走回调参数）*/
_attribute_ble_data_retention_ static u8 tsDataValue[1] = {0};

/* 写入包结构（与 SDK 的 ble_rf_packet_att_write_t 同布局）*/
typedef struct {
    u8  type;
    u8  rf_len;
    u16 l2capLen;
    u16 chanId;
    u8  opcode;
    u16 handle;
    u8  value;
} att_write_pkt_t;

/**
 * @brief 对时特征被写：协议 0x01 + Unix 秒（小端 4 字节）
 * 注意这里在 ATT 回调（中断上下文）里，只把值递给应用层，绝不做 flash/刷屏。
 */
static int on_ts_write(u16 connHandle, void *p)
{
    att_write_pkt_t *w = (att_write_pkt_t *)p;
    u16 len = (u16)(w->l2capLen - 3);
    u8  *v  = &w->value;
    unsigned int epoch;

    (void)connHandle;
    if (len < 5 || v[0] != 0x01) return 0;

    epoch = (unsigned int)v[1]
          | ((unsigned int)v[2] << 8)
          | ((unsigned int)v[3] << 16)
          | ((unsigned int)v[4] << 24);

    app_on_time_from_ble(epoch);
    return 0;
}

static const attribute_t my_Attributes[] = {
    {ATT_END_H - 1, 0, 0, 0, 0, 0, 0, 0},

    /* ── GAP 服务 ─────────────────────────────────────────────────────── */
    {7, ATT_PERMISSIONS_READ, 2, 2, (u8 *)&my_primaryServiceUUID, (u8 *)&my_gapServiceUUID, 0, 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(my_devNameCharVal),        (u8 *)&my_characterUUID,  (u8 *)my_devNameCharVal,        0, 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(my_devName),               (u8 *)&my_devNameUUID,    (u8 *)my_devName,               0, 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(my_appearanceCharVal),     (u8 *)&my_characterUUID,  (u8 *)my_appearanceCharVal,     0, 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(my_appearance),            (u8 *)&my_appearanceUIID, (u8 *)&my_appearance,           0, 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(my_periConnParamCharVal),  (u8 *)&my_characterUUID,  (u8 *)my_periConnParamCharVal,  0, 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(my_periConnParameters),    (u8 *)&my_periConnParamUUID, (u8 *)&my_periConnParameters, 0, 0},

    /* ── 对时服务 ─────────────────────────────────────────────────────── */
    {4, ATT_PERMISSIONS_READ, 2, 16, (u8 *)&my_primaryServiceUUID, (u8 *)tsServiceUUID, 0, 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(tsDataCharVal), (u8 *)&my_characterUUID, (u8 *)tsDataCharVal, 0, 0},
    {0, ATT_PERMISSIONS_RDWR, 16, 1, (u8 *)tsDataUUID, tsDataValue, (att_readwrite_callback_t)&on_ts_write, 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(tsDataDesc), (u8 *)&userdesc_UUID, (u8 *)tsDataDesc, 0, 0},
};

/* ══════════════════════════════════════════════════════════════════════════
 *  事件回调（只做最小必要处理）
 * ══════════════════════════════════════════════════════════════════════════ */
static int on_controller_event(u32 h, u8 *p, int n)
{
    (void)n;
    if (h & HCI_FLAG_EVENT_BT_STD) {
        u8 code = (u8)(h & 0xff);

        if (code == HCI_EVT_LE_META) {
            u8 sub = p[0];
            if (sub == HCI_SUB_EVT_LE_CONNECTION_COMPLETE) {
                hci_le_connectionCompleteEvt_t *evt = (hci_le_connectionCompleteEvt_t *)p;
                if (evt->status == BLE_SUCCESS && evt->role == ACL_ROLE_PERIPHERAL) {
                    /* 20~40（25~50ms）的间隔，兼顾写入响应速度与功耗 */
                    bls_l2cap_requestConnParamUpdate(evt->connHandle, 20, 40, 0, 400);
                }
            }
        }
    }
    return 0;
}

static int on_host_event(u32 h, u8 *para, int n)
{
    (void)h; (void)para; (void)n;
    return 0;
}

static void on_suspend_exit(u8 e, u8 *p, int n)
{
    (void)e; (void)p; (void)n;
    rf_set_power_level_index(RF_POWER_P0dBm);   /* 每次从 suspend 醒来重设发射功率 */
}

/* ══════════════════════════════════════════════════════════════════════════
 *  初始化
 * ══════════════════════════════════════════════════════════════════════════ */
void ble_app_adv(int on)
{
    blc_ll_setAdvEnable(on ? BLC_ADV_ENABLE : BLC_ADV_DISABLE);
}

void ble_app_init(void)
{
    u8 mac_public[6];
    u8 mac_random_static[6];

    blc_initMacAddress(flash_sector_mac_address, mac_public, mac_random_static);

    /* ── 链路层 ───────────────────────────────────────────────────────── */
    blc_ll_initBasicMCU();
    blc_ll_initStandby_module(mac_public);
    blc_ll_initLegacyAdvertising_module();
    blc_ll_initAclConnection_module();
    blc_ll_initAclPeriphrRole_module();

    blc_ll_setMaxConnectionNumber(ACL_CENTRAL_MAX_NUM, ACL_PERIPHR_MAX_NUM);
    blc_ll_setAclConnMaxOctetsNumber(ACL_CONN_MAX_RX_OCTETS,
                                     ACL_MASTER_MAX_TX_OCTETS,
                                     ACL_SLAVE_MAX_TX_OCTETS);

    blc_ll_initAclConnRxFifo(app_acl_rxfifo, ACL_RX_FIFO_SIZE, ACL_RX_FIFO_NUM);
    blc_ll_initAclPeriphrTxFifo(app_acl_slvTxfifo, ACL_SLAVE_TX_FIFO_SIZE,
                                ACL_SLAVE_TX_FIFO_NUM, ACL_PERIPHR_MAX_NUM);

    /* ── HCI ──────────────────────────────────────────────────────────── */
    blc_hci_registerControllerDataHandler(blc_l2cap_pktHandler);
    blc_hci_registerControllerEventHandler(on_controller_event);
    blc_hci_setEventMask_cmd(HCI_EVT_MASK_DISCONNECTION_COMPLETE);
    blc_hci_le_setEventMask_cmd(HCI_LE_EVT_MASK_CONNECTION_COMPLETE);

    /* ── 主机（GAP / L2CAP / ATT / GATT / SMP）─────────────────────────── */
    blc_gap_init();
    blc_l2cap_initAclConnSlaveMtuBuffer(mtu_s_rx_fifo, MTU_S_BUFF_SIZE,
                                        mtu_s_tx_fifo, MTU_S_BUFF_SIZE);
    blc_att_setSlaveRxMTUSize(ATT_MTU_SLAVE_RX_MAX_SIZE);

    bls_att_setAttributeTable((u8 *)my_Attributes);

    /* 不配对：特征权限里没有加密位，SMP 只需"无安全"，省掉配对流程 */
    blc_smp_configPairingSecurityInfoStorageAddressAndSize(flash_sector_smp_storage,
                                                          FLASH_SMP_PAIRING_MAX_SIZE);
    blc_smp_setSecurityLevel_slave(No_Security);
    blc_smp_smpParamInit();

    blc_gap_registerHostEventHandler(on_host_event);
    blc_gap_setEventMask(GAP_EVT_MASK_SMP_PAIRING_BEGIN);

    /* 栈初始化有任何错误这里会卡死（方便定位）*/
    blc_app_checkControllerHostInitialization();

    /* ── 广播 ─────────────────────────────────────────────────────────── */
    blc_ll_setAdvData((u8 *)s_adv_data, sizeof(s_adv_data));
    blc_ll_setScanRspData((u8 *)s_scan_rsp, sizeof(s_scan_rsp));
    blc_ll_setAdvParam(CFG_ADV_INTERVAL, CFG_ADV_INTERVAL,
                       ADV_TYPE_CONNECTABLE_UNDIRECTED, OWN_ADDRESS_PUBLIC,
                       0, NULL, BLT_ENABLE_ADV_ALL, ADV_FP_NONE);
    rf_set_power_level_index(RF_POWER_P0dBm);

    /* ── 功率管理 ─────────────────────────────────────────────────────── */
    /* 这里有三种活动时不进入 suspend：广播、扫描、主机连接。
     * 我们是纯从机，扫描/主机其实用不到，但 SDK 内部 IDLE 状态会用到这两个标志，
     * 保留它们不影响功耗（没有实际扫描/连接时不会阻止睡眠）。 */
    blc_ll_initPowerManagement_module();
    blc_pm_setSleepMask(PM_SLEEP_LEG_ADV | PM_SLEEP_LEG_SCAN | PM_SLEEP_ACL_MASTER);
    blc_ll_registerTelinkControllerEventCallback(BLT_EV_FLAG_SUSPEND_EXIT, &on_suspend_exit);

    blc_ll_setAdvEnable(BLC_ADV_ENABLE);
}
