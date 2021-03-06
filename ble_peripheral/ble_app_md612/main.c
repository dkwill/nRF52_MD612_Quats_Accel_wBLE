/* Copyright (c) 2014 Nordic Semiconductor. All Rights Reserved.
 *
 * The information contained herein is property of Nordic Semiconductor ASA.
 * Terms and conditions of usage are described in detail in NORDIC
 * SEMICONDUCTOR STANDARD SOFTWARE LICENSE AGREEMENT.
 *
 * Licensees are granted free, non-transferable use of the information. NO
 * WARRANTY of ANY KIND is provided. This heading must NOT be removed from
 * the file.
 *
 */

/** @file
 *
 * @defgroup ble_app_md612 main.c
 * @{
 * @brief md612_ble Sample Application main file.
 *
 * This file contains is the source code for a sample application using the md612, Battery and Device
 * Information Service for implementing a simple mouse functionality. This application uses the
 * @ref app_scheduler.
 *
 * Also it would accept pairing requests from any peer device. This implementation of the
 * application will not know whether a connected central is a known device or not.
 */

#include <stdint.h>
#include <string.h>
#include "nordic_common.h"
#include "nrf.h"
#include "nrf_soc.h"
#include "nrf_sdm.h"
#include "app_error.h"
#include "nrf_gpio.h"
#include "ble.h"
#include "ble_srv_common.h"
#include "ble_advdata.h"
#include "ble_bas.h"
#include "ble_conn_params.h"
#include "bsp.h"
#include "sensorsim.h"
#include "bsp_btn_ble.h"
#include "app_scheduler.h"
#include "softdevice_handler_appsh.h"
#include "app_timer_appsh.h"
#include "peer_manager.h"
#include "app_button.h"
#include "ble_advertising.h"
#include "fds.h"
#include "fstorage.h"
#include "ble_conn_state.h"

#include "md612.h"
#include "app_twi.h"

#define NRF_LOG_MODULE_NAME "MD612_BLE"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"

#include "packet.h"
#include "timestamping.h"

#include "mlmath.h"
#include "ml_math_func.h"
#include "inv_mpu_dmp_motion_driver.h"

#include "nrf_delay.h"

#if BUTTONS_NUMBER < 4
#error "Not enough resources on board to run example"
#endif

/*
 * Bluetooth Defines
 */
#if (NRF_SD_BLE_API_VERSION == 3)
#define NRF_BLE_MAX_MTU_SIZE            GATT_MTU_SIZE_DEFAULT                      /**< MTU size used in the softdevice enabling and to reply to a BLE_GATTS_EVT_EXCHANGE_MTU_REQUEST event. */
#endif

#define IS_SRVC_CHANGED_CHARACT_PRESENT 1                                          /**< Include or not the service_changed characteristic. if not enabled, the server's database cannot be changed for the lifetime of the device*/

#define CENTRAL_LINK_COUNT              0                                          /**< Number of central links used by the application. When changing this number remember to adjust the RAM settings*/
#define PERIPHERAL_LINK_COUNT           1                                          /**< Number of peripheral links used by the application. When changing this number remember to adjust the RAM settings*/

#define DEVICE_NAME                     "Nordic_MD612"
#define MANUFACTURER_NAME               "NordicSemiconductor"                      /**< Manufacturer. Will be passed to Device Information Service. */

#define APP_ADV_FAST_TIMEOUT            30                                         /**< The duration of the fast advertising period (in seconds). */
#define APP_ADV_SLOW_TIMEOUT            180                                        /**< The duration of the slow advertising period (in seconds). */

#define APP_ADV_FAST_INTERVAL           0x0028                                                    /**< Fast advertising interval (in units of 0.625 ms. This value corresponds to 25 ms.). */
#define APP_ADV_SLOW_INTERVAL           0x0C80                                                    /**< Slow advertising interval (in units of 0.625 ms. This value corrsponds to 2 seconds). */

#define APP_TIMER_PRESCALER             0                                          /**< Value of the RTC1 PRESCALER register. */
#define APP_TIMER_OP_QUEUE_SIZE         4                                          /**< Size of timer operation queues. */

/*lint -emacro(524, MIN_CONN_INTERVAL) // Loss of precision */
#define MIN_CONN_INTERVAL               MSEC_TO_UNITS(7.5, UNIT_1_25_MS)            /**< Minimum connection interval (7.5 ms). */
#define MAX_CONN_INTERVAL               MSEC_TO_UNITS(15, UNIT_1_25_MS)             /**< Maximum connection interval (15 ms). */
#define SLAVE_LATENCY                   20                                          /**< Slave latency (20ms). */
#define CONN_SUP_TIMEOUT                MSEC_TO_UNITS(3000, UNIT_10_MS)             /**< Connection supervisory timeout (3000 ms). */

#define FIRST_CONN_PARAMS_UPDATE_DELAY  APP_TIMER_TICKS(5000, APP_TIMER_PRESCALER)  /**< Time from initiating event (connect or start of notification) to first time sd_ble_gap_conn_param_update is called (5 seconds). */
#define NEXT_CONN_PARAMS_UPDATE_DELAY   APP_TIMER_TICKS(30000, APP_TIMER_PRESCALER) /**< Time between each call to sd_ble_gap_conn_param_update after the first call (30 seconds). */
#define MAX_CONN_PARAM_UPDATE_COUNT     3                                           /**< Number of attempts before giving up the connection parameter negotiation. */

#define SEC_PARAM_BOND                  1                                           /**< Perform bonding. */
#define SEC_PARAM_MITM                  0                                           /**< Man In The Middle protection not required. */
#define SEC_PARAM_LESC                  0                                           /**< LE Secure Connections not enabled. */
#define SEC_PARAM_KEYPRESS              0                                           /**< Keypress notifications not enabled. */
#define SEC_PARAM_IO_CAPABILITIES       BLE_GAP_IO_CAPS_NONE                        /**< No I/O capabilities. */
#define SEC_PARAM_OOB                   0                                           /**< Out Of Band data not available. */
#define SEC_PARAM_MIN_KEY_SIZE          7                                           /**< Minimum encryption key size. */
#define SEC_PARAM_MAX_KEY_SIZE          16                                          /**< Maximum encryption key size. */

#define SCHED_MAX_EVENT_DATA_SIZE       MAX(APP_TIMER_SCHED_EVT_SIZE, \
                                            BLE_STACK_HANDLER_SCHED_EVT_SIZE)         /**< Maximum size of scheduler events. */
#define SCHED_QUEUE_SIZE                10                                            /**< Maximum number of events in the scheduler queue. */


#define DEAD_BEEF                       0xDEADBEEF                                    /**< Value used as error code on stack dump, can be used to identify stack location on stack unwind. */

#define BLE_UUID_BASE_UUID              {{0x23, 0xD1, 0x13, 0xEF, 0x5F, 0x78,  0x23, 0x15,0xDE, 0xEF,0x12, 0x12, 0x00, 0x00, 0x00, 0x00}} 			// 128-bit base UUID

#define BLE_UUID_MDE_SERVICE_UUID			0xF00D // Just a random, but recognizable value
#define BLE_UUID_X_CHARACTERISTC_UUID    	0xACCE
#define BLE_UUID_Y_CHARACTERISTC_UUID    	0xBEEF
#define BLE_UUID_Z_CHARACTERISTC_UUID 		0xCEEF
#define BLE_UUID_YAWR_CHARACTERISTC_UUID 	0xDEEF  // reset Yaw Reset

#define APP_FEATURE_NOT_SUPPORTED       	BLE_GATT_STATUS_ATTERR_APP_BEGIN + 2                      /**< Reply when unsupported features are requested. */

/*
 * Battery BLE definitions
 */
#define BATTERY_LEVEL_MEAS_INTERVAL     APP_TIMER_TICKS(2000, APP_TIMER_PRESCALER) /**< Battery level measurement interval (ticks). */
#define MIN_BATTERY_LEVEL               81                                         /**< Minimum simulated battery level. */
#define MAX_BATTERY_LEVEL               100                                        /**< Maximum simulated battery level. */
#define BATTERY_LEVEL_INCREMENT         1                                          /**< Increment between each simulated battery level measurement. */

/*
 * Defines for the MPU Board and NRF52
 */

#ifdef BOARD_PCA10040
#undef ARDUINO_SCL_PIN
#undef ARDUINO_SDA_PIN
#define ARDUINO_SCL_PIN             	3    // SCL signal pin
#define ARDUINO_SDA_PIN            	 	4    // SDA signal pin
#define MPU_INT_PIN                     11

//#define ARDUINO_SCL_PIN             	26    // SCL signal pin
//#define ARDUINO_SDA_PIN             	27    // SDA signal pin
//#define MPU_INT_PIN                   11

#else
#define MPU_INT_PIN                     10
#endif

#ifdef NRF_LOG_BACKEND_SERIAL_USES_UART
#define UART_TX_BUF_SIZE                256                                        /**< UART TX buffer size. */
#define UART_RX_BUF_SIZE                1                                          /**< UART RX buffer size. */

#endif

#define TWI_MAX_PENDING_TRANSACTIONS    5

//  our BLE service variable and other BLE variables
typedef struct {
	uint16_t conn_handle; 						/**< Handle of the current connection (as provided by the BLE stack, is BLE_CONN_HANDLE_INVALID if not in a connection).*/
	uint16_t service_handle; 					/**< Handle of ble Service (as provided by the BLE stack). */
	ble_gatts_char_handles_t x_char_handles; 	/**< Handles related to the our new characteristic. */
	ble_gatts_char_handles_t y_char_handles; 	/**< Handles related to the our new characteristic. */
	ble_gatts_char_handles_t z_char_handles; 	/**< Handles related to the our new characteristic. */
//DKW - add characteristics? Maybe change to qw,qx,qy,qz and ax, ay, az (or is there a better way?)
} ble_mde_t;

static ble_mde_t m_mde; 						/**< MDE BLE Information. */

static pm_peer_id_t m_peer_id; 												/**< Device reference handle to the current bonded central. */

static pm_peer_id_t m_whitelist_peers[BLE_GAP_WHITELIST_ADDR_MAX_COUNT]; 	/**< List of peers currently in the whitelist. */
static uint32_t m_whitelist_peer_cnt; 										/**< Number of peers currently in the whitelist. */
static bool m_is_wl_changed; 												/**< Indicates if the whitelist has been changed since last time it has been updated in the Peer Manager. */

/*
 * Battery variables
 */
static ble_bas_t m_bas; 													/**< Structure used to identify the battery service. */
static sensorsim_cfg_t m_battery_sim_cfg; 									/**< Battery Level sensor simulator configuration. */
static sensorsim_state_t m_battery_sim_state; 								/**< Battery Level sensor simulator state. */

APP_TIMER_DEF(m_battery_timer_id); 											/**< Battery timer. */

/*
 * twi interface variables
 */
app_twi_t m_app_twi = APP_TWI_INSTANCE(0);

// call back function used by the MD612 to notify of changes.
static void motiondriver_callback(unsigned char type, long *data,
		int8_t accuracy, unsigned long timestamp);

// Pulled out of function has to exist even after fucntion exits.
static platform_data_t const platform_data = {
		.pin = MPU_INT_PIN,
		.cb = motiondriver_callback,

/* The sensors can be mounted onto the board in any orientation. The mounting
 * matrix seen below tells the MPL how to rotate the raw data from the
 * driver(s).
 */
.gyro_orientation = { 1, 0, 0, 0, 1, 0, 0, 0, 1 },

#if defined MPU9150 || defined MPU9250
		.compass_orientation = {0, 1, 0,
			1, 0, 0,
			0, 0,-1}

#elif defined AK8975_SECONDARY
		.compass_orientation = {-1, 0, 0,
			0, 1, 0,
			0, 0,-1}

#elif defined AK8963_SECONDARY
		.compass_orientation = {-1, 0, 0,
			0,-1, 0,
			0, 0, 1}
#endif
	};

/**@brief Callback function for asserts in the SoftDevice.
 *
 * @details This function will be called in case of an assert in the SoftDevice.
 *
 * @warning This handler is an example only and does not fit a final product. You need to analyze
 *          how your product is supposed to react in case of Assert.
 * @warning On assert from the SoftDevice, the system can only recover on reset.
 *
 * @param[in]   line_num   Line number of the failing ASSERT call.
 * @param[in]   file_name  File name of the failing ASSERT call.
 */
void assert_nrf_callback(uint16_t line_num, const uint8_t * p_file_name) {
	app_error_handler(DEAD_BEEF, line_num, p_file_name);
}

/**@brief Fetch the list of peer manager peer IDs.
 *
 * @param[inout] p_peers   The buffer where to store the list of peer IDs.
 * @param[inout] p_size    In: The size of the @p p_peers buffer.
 *                         Out: The number of peers copied in the buffer.
 */
static void peer_list_get(pm_peer_id_t * p_peers, uint32_t * p_size) {
	pm_peer_id_t peer_id;
	uint32_t peers_to_copy;

	peers_to_copy =	(*p_size < BLE_GAP_WHITELIST_ADDR_MAX_COUNT) ? 	*p_size : BLE_GAP_WHITELIST_ADDR_MAX_COUNT;

	peer_id = pm_next_peer_id_get(PM_PEER_ID_INVALID);
	*p_size = 0;

	while ((peer_id != PM_PEER_ID_INVALID) && (peers_to_copy--)) {
		p_peers[(*p_size)++] = peer_id;
		peer_id = pm_next_peer_id_get(peer_id);
	}
}

/**@brief Function for starting advertising.
 */
static void advertising_start(void) {
	ret_code_t ret;

	memset(m_whitelist_peers, PM_PEER_ID_INVALID, sizeof(m_whitelist_peers));
	m_whitelist_peer_cnt = (sizeof(m_whitelist_peers) / sizeof(pm_peer_id_t));

	peer_list_get(m_whitelist_peers, &m_whitelist_peer_cnt);

	ret = pm_whitelist_set(m_whitelist_peers, m_whitelist_peer_cnt);
	APP_ERROR_CHECK(ret);

	// Setup the device identies list.
	// Some SoftDevices do not support this feature.
	ret = pm_device_identities_list_set(m_whitelist_peers,
			m_whitelist_peer_cnt);
	if (ret != NRF_ERROR_NOT_SUPPORTED) {
		APP_ERROR_CHECK(ret);
	}

	m_is_wl_changed = false;

	ret = ble_advertising_start(BLE_ADV_MODE_FAST);
	APP_ERROR_CHECK(ret);
}

/**@brief Function for handling Peer Manager events.
 *
 * @param[in] p_evt  Peer Manager event.
 */
static void pm_evt_handler(pm_evt_t const * p_evt) {
	ret_code_t err_code;

	NRF_LOG_INFO("pm_evt_Handler %d\r\n", p_evt->evt_id);

	switch (p_evt->evt_id) {
	case PM_EVT_BONDED_PEER_CONNECTED: {
		NRF_LOG_DEBUG("Connected to previously bonded device\r\n");
		err_code = pm_peer_rank_highest(p_evt->peer_id);
		if (err_code != NRF_ERROR_BUSY) {
			APP_ERROR_CHECK(err_code);
		}
	}
		break; // PM_EVT_BONDED_PEER_CONNECTED

	case PM_EVT_CONN_SEC_START:
		NRF_LOG_INFO("PM_EVT_CONN_SEC_START %d\r\n", p_evt->evt_id)
		;
		break; // PM_EVT_CONN_SEC_START

	case PM_EVT_CONN_SEC_SUCCEEDED: {
		NRF_LOG_DEBUG(
				"Link secured. Role: %d. conn_handle: %d, Procedure: %d\r\n",
				ble_conn_state_role(p_evt->conn_handle), p_evt->conn_handle,
				p_evt->params.conn_sec_succeeded.procedure);
		err_code = pm_peer_rank_highest(p_evt->peer_id);
		if (err_code != NRF_ERROR_BUSY) {
			APP_ERROR_CHECK(err_code);
		}
		if (p_evt->params.conn_sec_succeeded.procedure
				== PM_LINK_SECURED_PROCEDURE_BONDING) {
			NRF_LOG_DEBUG(
					"New Bond, add the peer to the whitelist if possible\r\n");
			NRF_LOG_DEBUG("\tm_whitelist_peer_cnt %d, MAX_PEERS_WLIST %d\r\n",
					m_whitelist_peer_cnt + 1, BLE_GAP_WHITELIST_ADDR_MAX_COUNT);
			if (m_whitelist_peer_cnt < BLE_GAP_WHITELIST_ADDR_MAX_COUNT) {
				//bonded to a new peer, add it to the whitelist.
				m_whitelist_peers[m_whitelist_peer_cnt++] = m_peer_id;
				m_is_wl_changed = true;
			}
			//Note: This code will use the older bonded device in the white list and not add any newer bonded to it
			//      You should check on what kind of white list policy your application should use.
		}
	}
		break; // PM_EVT_CONN_SEC_SUCCEEDED

	case PM_EVT_CONN_SEC_FAILED: {
		/** In some cases, when securing fails, it can be restarted directly. Sometimes it can
		 *  be restarted, but only after changing some Security Parameters. Sometimes, it cannot
		 *  be restarted until the link is disconnected and reconnected. Sometimes it is
		 *  impossible, to secure the link, or the peer device does not support it. How to
		 *  handle this error is highly application dependent. */
		switch (p_evt->params.conn_sec_failed.error) {
		case PM_CONN_SEC_ERROR_PIN_OR_KEY_MISSING:
			// Rebond if one party has lost its keys.
			err_code = pm_conn_secure(p_evt->conn_handle, true);
			if (err_code != NRF_ERROR_INVALID_STATE) {
				APP_ERROR_CHECK(err_code);
			}
			break; // PM_CONN_SEC_ERROR_PIN_OR_KEY_MISSING

		default:
			break;
		}
	}
		break; // PM_EVT_CONN_SEC_FAILED

	case PM_EVT_CONN_SEC_CONFIG_REQ: {
		// Reject pairing request from an already bonded peer.
		pm_conn_sec_config_t conn_sec_config = { .allow_repairing = false };
		pm_conn_sec_config_reply(p_evt->conn_handle, &conn_sec_config);
	}
		break; // PM_EVT_CONN_SEC_CONFIG_REQ

	case PM_EVT_STORAGE_FULL: {
		// Run garbage collection on the flash.
		err_code = fds_gc();
		if (err_code == FDS_ERR_BUSY
				|| err_code == FDS_ERR_NO_SPACE_IN_QUEUES) {
			// Retry.
		} else {
			APP_ERROR_CHECK(err_code);
		}
	}
		break; // PM_EVT_STORAGE_FULL

	case PM_EVT_ERROR_UNEXPECTED:
		// Assert.
		APP_ERROR_CHECK(p_evt->params.error_unexpected.error);
		break; // PM_EVT_ERROR_UNEXPECTED

	case PM_EVT_PEER_DATA_UPDATE_SUCCEEDED:
		NRF_LOG_INFO("PM_EVT_PEER_DATA_UPDATE_SUCCEEDED %d\r\n", p_evt->evt_id)
		;
		break; // PM_EVT_PEER_DATA_UPDATE_SUCCEEDED

	case PM_EVT_PEER_DATA_UPDATE_FAILED:
		// Assert.
		APP_ERROR_CHECK_BOOL(false);
		break; // PM_EVT_PEER_DATA_UPDATE_FAILED

	case PM_EVT_PEER_DELETE_SUCCEEDED:
		break; // PM_EVT_PEER_DELETE_SUCCEEDED

	case PM_EVT_PEER_DELETE_FAILED:
		// Assert.
		APP_ERROR_CHECK(p_evt->params.peer_delete_failed.error);
		break; // PM_EVT_PEER_DELETE_FAILED

	case PM_EVT_PEERS_DELETE_SUCCEEDED:
		advertising_start();
		break; // PM_EVT_PEERS_DELETE_SUCCEEDED

	case PM_EVT_PEERS_DELETE_FAILED:
		// Assert.
		APP_ERROR_CHECK(p_evt->params.peers_delete_failed_evt.error);
		break; // PM_EVT_PEERS_DELETE_FAILED

	case PM_EVT_LOCAL_DB_CACHE_APPLIED:
		break; // PM_EVT_LOCAL_DB_CACHE_APPLIED

	case PM_EVT_LOCAL_DB_CACHE_APPLY_FAILED:
		// The local database has likely changed, send service changed indications.
		pm_local_database_has_changed();
		break; // PM_EVT_LOCAL_DB_CACHE_APPLY_FAILED

	case PM_EVT_SERVICE_CHANGED_IND_SENT:
		break; // PM_EVT_SERVICE_CHANGED_IND_SENT

	case PM_EVT_SERVICE_CHANGED_IND_CONFIRMED:
		break; // PM_EVT_SERVICE_CHANGED_IND_SENT

	default:
		NRF_LOG_INFO("Default %d\r\n", p_evt->evt_id)
		;
		// No implementation needed.
		break;
	}
}

/**@brief Function for handling advertising errors.
 *
 * @param[in] nrf_error  Error code containing information about what went wrong.
 */
static void ble_advertising_error_handler(uint32_t nrf_error) {
	APP_ERROR_HANDLER(nrf_error);
}

/**@brief Function for performing a battery measurement, and update the Battery Level characteristic in the Battery Service.
 */
static void battery_level_update(void) {
	uint32_t err_code;
	uint8_t battery_level;

	battery_level = (uint8_t) sensorsim_measure(&m_battery_sim_state, &m_battery_sim_cfg);

	err_code = ble_bas_battery_level_update(&m_bas, battery_level);
	if ((err_code != NRF_SUCCESS) && (err_code != NRF_ERROR_INVALID_STATE)
			&& (err_code != BLE_ERROR_NO_TX_PACKETS)
			&& (err_code != BLE_ERROR_GATTS_SYS_ATTR_MISSING)) {
		APP_ERROR_HANDLER(err_code);
	}
}

/**@brief Function for handling the Battery measurement timer timeout.
 *
 * @details This function will be called each time the battery level measurement timer expires.
 *
 * @param[in]   p_context   Pointer used for passing some arbitrary information (context) from the
 *                          app_start_timer() call to the timeout handler.
 */
static void battery_level_meas_timeout_handler(void * p_context) {
	UNUSED_PARAMETER(p_context);
	battery_level_update();
}

/**@brief Function for the Timer initialization.
 *
 * @details Initializes the timer module.
 */
static void timers_init(void) {
	uint32_t err_code;

	// Initialize timer module, making it use the scheduler.
	APP_TIMER_APPSH_INIT(APP_TIMER_PRESCALER, APP_TIMER_OP_QUEUE_SIZE, true);

	// Create battery timer.
	err_code = app_timer_create(&m_battery_timer_id, APP_TIMER_MODE_REPEATED,
			battery_level_meas_timeout_handler);
	APP_ERROR_CHECK(err_code);
}

/**@brief Function for the GAP initialization.
 *
 * @details This function sets up all the necessary GAP (Generic Access Profile) parameters of the
 *          device including the device name, appearance, and the preferred connection parameters.
 */
static void gap_params_init(void) {
	uint32_t err_code;
	ble_gap_conn_params_t gap_conn_params;
	ble_gap_conn_sec_mode_t sec_mode;

	BLE_GAP_CONN_SEC_MODE_SET_OPEN(&sec_mode);

	err_code = sd_ble_gap_device_name_set(&sec_mode,
			(const uint8_t *) DEVICE_NAME, strlen(DEVICE_NAME));
	APP_ERROR_CHECK(err_code);

	memset(&gap_conn_params, 0, sizeof(gap_conn_params));

	gap_conn_params.min_conn_interval = MIN_CONN_INTERVAL;
	gap_conn_params.max_conn_interval = MAX_CONN_INTERVAL;
	gap_conn_params.slave_latency = SLAVE_LATENCY;
	gap_conn_params.conn_sup_timeout = CONN_SUP_TIMEOUT;

	err_code = sd_ble_gap_ppcp_set(&gap_conn_params);
	APP_ERROR_CHECK(err_code);
}

/**@brief Function for initializing Battery Service.
 */
static void bas_init(void) {
	uint32_t err_code;
	ble_bas_init_t bas_init_obj;

	memset(&bas_init_obj, 0, sizeof(bas_init_obj));

	bas_init_obj.evt_handler = NULL;
	bas_init_obj.support_notification = true;
	bas_init_obj.p_report_ref = NULL;
	bas_init_obj.initial_batt_level = 100;

	BLE_GAP_CONN_SEC_MODE_SET_ENC_NO_MITM(
			&bas_init_obj.battery_level_char_attr_md.cccd_write_perm);
	BLE_GAP_CONN_SEC_MODE_SET_ENC_NO_MITM(
			&bas_init_obj.battery_level_char_attr_md.read_perm);
	BLE_GAP_CONN_SEC_MODE_SET_NO_ACCESS(
			&bas_init_obj.battery_level_char_attr_md.write_perm);

	BLE_GAP_CONN_SEC_MODE_SET_ENC_NO_MITM(
			&bas_init_obj.battery_level_report_read_perm);

	err_code = ble_bas_init(&m_bas, &bas_init_obj);
	APP_ERROR_CHECK(err_code);
}

/**@brief Function for adding our new characteristic to "Our service"
 *
 * @param[in]   p_mde        mde structure.
 *
 */
static uint32_t ble_char_mde_add(ble_mde_t * p_mde) {

	uint32_t err_code = 0; 		// Variable to hold return codes from library and softdevice functions
	ble_uuid_t char_uuid;

	ble_uuid128_t base_uuid = BLE_UUID_BASE_UUID;

	// setup the 1st characteristic UUID.
	BLE_UUID_BLE_ASSIGN(char_uuid, BLE_UUID_X_CHARACTERISTC_UUID);
	sd_ble_uuid_vs_add(&base_uuid, &char_uuid.type);
	APP_ERROR_CHECK(err_code);

	// setup the attribute metadata - permissions, auth levels, variable length
	// or not and where in memory stored - storing information in stack
	ble_gatts_attr_md_t attr_md;
	memset(&attr_md, 0, sizeof(attr_md));
	attr_md.vloc = BLE_GATTS_VLOC_STACK;  // store information in stack other option (BLE_GATTS_VLOC_USER)

	BLE_GAP_CONN_SEC_MODE_SET_OPEN(&attr_md.read_perm);
	BLE_GAP_CONN_SEC_MODE_SET_OPEN(&attr_md.write_perm);

	// Configure the charateristic value attribute - contains the actual value..
	ble_gatts_attr_t attr_char_value;
	memset(&attr_char_value, 0, sizeof(attr_char_value));
	attr_char_value.p_uuid = &char_uuid;
	attr_char_value.p_attr_md = &attr_md;
	attr_char_value.max_len = sizeof (int16_t);
	attr_char_value.init_len = sizeof (int16_t);
	uint8_t value[2] = { 1 };                   // just for testing set to 1.
	attr_char_value.p_value = value;

	ble_gatts_char_md_t char_md;
	memset(&char_md, 0, sizeof(char_md));
	char_md.char_props.read = 1;
	char_md.char_props.write = 1;

	// setup the attribute metadata for charaterictic
	ble_gatts_attr_md_t cccd_md;
	memset(&cccd_md, 0, sizeof(cccd_md));
	BLE_GAP_CONN_SEC_MODE_SET_OPEN(&cccd_md.read_perm);
	BLE_GAP_CONN_SEC_MODE_SET_OPEN(&cccd_md.write_perm);
	cccd_md.vloc = BLE_GATTS_VLOC_STACK;
	char_md.p_cccd_md = &cccd_md;
	char_md.char_props.notify = 1;

	// Add up X characteristic
	err_code = sd_ble_gatts_characteristic_add(p_mde->service_handle, &char_md,
			&attr_char_value, &p_mde->x_char_handles);
	APP_ERROR_CHECK(err_code);

	// setup the Y characteristic UUID.
	BLE_UUID_BLE_ASSIGN(char_uuid, BLE_UUID_Y_CHARACTERISTC_UUID);
	sd_ble_uuid_vs_add(&base_uuid, &char_uuid.type);
	APP_ERROR_CHECK(err_code);
	// add up Y characteristic
	value[0] = 2;
	value[1] = 2;  // just for testing set to 2.
	err_code = sd_ble_gatts_characteristic_add(p_mde->service_handle, &char_md,
				&attr_char_value, &p_mde->y_char_handles);
	APP_ERROR_CHECK(err_code);

	// setup the Z characteristic UUID.
	BLE_UUID_BLE_ASSIGN(char_uuid, BLE_UUID_Z_CHARACTERISTC_UUID);
	sd_ble_uuid_vs_add(&base_uuid, &char_uuid.type);
	APP_ERROR_CHECK(err_code);
	value[0] = 3;                   // just for testing set to 3.
	value[1] = 3;
	// add up Y characteristic
	err_code = sd_ble_gatts_characteristic_add(p_mde->service_handle, &char_md,
				&attr_char_value, &p_mde->z_char_handles);
	APP_ERROR_CHECK(err_code);

	return NRF_SUCCESS;
}

/**@brief Function for initiating our new service.
 *
 * @param[in]   p_mde        Our Service structure.
 *
 */
void ble_mde_service_init(ble_mde_t * p_mde) {
	uint32_t err_code; // Variable to hold return codes from library and softdevice functions

	ble_uuid_t service_uuid;
	ble_uuid128_t base_uuid = BLE_UUID_BASE_UUID;
	service_uuid.uuid = BLE_UUID_MDE_SERVICE_UUID;

	err_code = sd_ble_uuid_vs_add(&base_uuid, &service_uuid.type);
	APP_ERROR_CHECK(err_code);

	// set to invalid handle 0xffff
	p_mde->conn_handle = BLE_CONN_HANDLE_INVALID;

	err_code = sd_ble_gatts_service_add(BLE_GATTS_SRVC_TYPE_PRIMARY,
			&service_uuid, &p_mde->service_handle);

	APP_ERROR_CHECK(err_code);

	NRF_LOG_INFO("Service UUID: 0x%#04x\n", service_uuid.uuid);
	NRF_LOG_INFO("Service UUID type: 0x%#02x\n", service_uuid.type);
	NRF_LOG_INFO("Service handle: 0x%#04x\n", p_mde->service_handle);


	ble_char_mde_add(p_mde);
}

/**@brief Function for initializing services that will be used by the application.
 */
static void services_init(void) {

	ble_mde_service_init(&m_mde);
	bas_init();

}

/**@brief Function for initializing the battery sensor simulator.
 */
static void sensor_simulator_init(void) {
	m_battery_sim_cfg.min = MIN_BATTERY_LEVEL;
	m_battery_sim_cfg.max = MAX_BATTERY_LEVEL;
	m_battery_sim_cfg.incr = BATTERY_LEVEL_INCREMENT;
	m_battery_sim_cfg.start_at_max = true;

	sensorsim_init(&m_battery_sim_state, &m_battery_sim_cfg);
}

/**@brief Function for handling a Connection Parameters error.
 *
 * @param[in]   nrf_error   Error code containing information about what went wrong.
 */
static void conn_params_error_handler(uint32_t nrf_error) {
	APP_ERROR_HANDLER(nrf_error);
}

/**@brief Function for initializing the Connection Parameters module.
 */
static void conn_params_init(void) {
	uint32_t err_code;
	ble_conn_params_init_t cp_init;

	memset(&cp_init, 0, sizeof(cp_init));

	cp_init.p_conn_params = NULL;
	cp_init.first_conn_params_update_delay = FIRST_CONN_PARAMS_UPDATE_DELAY;
	cp_init.next_conn_params_update_delay = NEXT_CONN_PARAMS_UPDATE_DELAY;
	cp_init.max_conn_params_update_count = MAX_CONN_PARAM_UPDATE_COUNT;
	cp_init.start_on_notify_cccd_handle = BLE_GATT_HANDLE_INVALID;
	cp_init.disconnect_on_fail = false;
	cp_init.evt_handler = NULL;
	cp_init.error_handler = conn_params_error_handler;

	err_code = ble_conn_params_init(&cp_init);
	APP_ERROR_CHECK(err_code);
}

/**@brief Function for starting timers.
 */
static void timers_start(void) {
	uint32_t err_code;

	err_code = app_timer_start(m_battery_timer_id, BATTERY_LEVEL_MEAS_INTERVAL,
			NULL);
	APP_ERROR_CHECK(err_code);
}

/**@brief Function for putting the chip into sleep mode.
 *
 * @note This function will not return.
 */
static void sleep_mode_enter(void) {
	uint32_t err_code = bsp_indication_set(BSP_INDICATE_IDLE);

	NRF_LOG_INFO ("sleep 1\r\n");
	APP_ERROR_CHECK(err_code);

	// Prepare wakeup buttons.
	err_code = bsp_btn_ble_sleep_mode_prepare();
	NRF_LOG_INFO ("sleep 2\r\n");
	APP_ERROR_CHECK(err_code);

	// Go to system-off mode (this function will not return; wakeup will cause a reset).
	err_code = sd_power_system_off();
	NRF_LOG_INFO ("sleep 3\r\n");
	APP_ERROR_CHECK(err_code);
	NRF_LOG_INFO ("sleep 4\r\n");
}


/**@brief Function for handling advertising events.
 *
 * @details This function will be called for advertising events which are passed to the application.
 *
 * @param[in] ble_adv_evt  Advertising event.
 */
static void on_adv_evt(ble_adv_evt_t ble_adv_evt) {
	uint32_t err_code;

	switch (ble_adv_evt) {
	case BLE_ADV_EVT_DIRECTED:
		NRF_LOG_INFO("BLE_ADV_EVT_DIRECTED\r\n");
		err_code = bsp_indication_set(BSP_INDICATE_ADVERTISING_DIRECTED);
		APP_ERROR_CHECK(err_code);
		break;

	case BLE_ADV_EVT_FAST:
		NRF_LOG_INFO("BLE_ADV_EVT_FAST\r\n")
		;
		err_code = bsp_indication_set(BSP_INDICATE_ADVERTISING);
		APP_ERROR_CHECK(err_code);
		break;

	case BLE_ADV_EVT_SLOW:
		NRF_LOG_INFO("BLE_ADV_EVT_SLOW\r\n");
		err_code = bsp_indication_set(BSP_INDICATE_ADVERTISING_SLOW);
		APP_ERROR_CHECK(err_code);
		break;

	case BLE_ADV_EVT_FAST_WHITELIST:
		NRF_LOG_INFO("BLE_ADV_EVT_FAST_WHITELIST\r\n");
		err_code = bsp_indication_set(BSP_INDICATE_ADVERTISING_WHITELIST);
		APP_ERROR_CHECK(err_code);
		break;

	case BLE_ADV_EVT_SLOW_WHITELIST:
		NRF_LOG_INFO("BLE_ADV_EVT_SLOW_WHITELIST\r\n");
		err_code = bsp_indication_set(BSP_INDICATE_ADVERTISING_WHITELIST);
		APP_ERROR_CHECK(err_code);
		err_code = ble_advertising_restart_without_whitelist();
		APP_ERROR_CHECK(err_code);
		break;

	case BLE_ADV_EVT_IDLE:
		err_code = bsp_indication_set(BSP_INDICATE_IDLE);
		APP_ERROR_CHECK(err_code);
		sleep_mode_enter();
		break;

	case BLE_ADV_EVT_WHITELIST_REQUEST: {
		ble_gap_addr_t whitelist_addrs[BLE_GAP_WHITELIST_ADDR_MAX_COUNT];
		ble_gap_irk_t whitelist_irks[BLE_GAP_WHITELIST_ADDR_MAX_COUNT];
		uint32_t addr_cnt = BLE_GAP_WHITELIST_ADDR_MAX_COUNT;
		uint32_t irk_cnt = BLE_GAP_WHITELIST_ADDR_MAX_COUNT;

		err_code = pm_whitelist_get(whitelist_addrs, &addr_cnt, whitelist_irks,
				&irk_cnt);
		APP_ERROR_CHECK(err_code);
		NRF_LOG_DEBUG("pm_whitelist_get returns %d addr in whitelist and %d irk whitelist\r\n",
				addr_cnt, irk_cnt);

		// Apply the whitelist.
		err_code = ble_advertising_whitelist_reply(whitelist_addrs, addr_cnt,
				whitelist_irks, irk_cnt);
		APP_ERROR_CHECK(err_code);
	}
		break;

	case BLE_ADV_EVT_PEER_ADDR_REQUEST: {
		pm_peer_data_bonding_t peer_bonding_data;

		// Only Give peer address if we have a handle to the bonded peer.
		if (m_peer_id != PM_PEER_ID_INVALID) {

			err_code = pm_peer_data_bonding_load(m_peer_id, &peer_bonding_data);
			if (err_code != NRF_ERROR_NOT_FOUND) {
				APP_ERROR_CHECK(err_code);

				ble_gap_addr_t * p_peer_addr =
						&(peer_bonding_data.peer_id.id_addr_info);
				err_code = ble_advertising_peer_addr_reply(p_peer_addr);
				APP_ERROR_CHECK(err_code);
			}

		}
		break;
	}

	default:
		break;
	}
}

/**@brief Function for handling the Application's BLE Stack events.
 *
 * @param[in]   p_ble_evt   Bluetooth stack event.
 */
static void on_ble_evt(ble_evt_t * p_ble_evt) {
	uint32_t err_code;

	switch (p_ble_evt->header.evt_id) {
	case BLE_GAP_EVT_CONNECTED:
		NRF_LOG_INFO("Connected\r\n");
		err_code = bsp_indication_set(BSP_INDICATE_CONNECTED);
		APP_ERROR_CHECK(err_code);

		m_mde.conn_handle = p_ble_evt->evt.gap_evt.conn_handle;
		break; // BLE_GAP_EVT_CONNECTED

	case BLE_GAP_EVT_DISCONNECTED:
		NRF_LOG_INFO("Disconnected\r\n");
		err_code = bsp_indication_set(BSP_INDICATE_IDLE);
		APP_ERROR_CHECK(err_code);

		m_mde.conn_handle = BLE_CONN_HANDLE_INVALID;

		if (m_is_wl_changed) {
			// The whitelist has been modified, update it in the Peer Manager.
			err_code = pm_whitelist_set(m_whitelist_peers,
					m_whitelist_peer_cnt);
			APP_ERROR_CHECK(err_code);

			err_code = pm_device_identities_list_set(m_whitelist_peers,
					m_whitelist_peer_cnt);
			if (err_code != NRF_ERROR_NOT_SUPPORTED) {
				APP_ERROR_CHECK(err_code);
			}

			m_is_wl_changed = false;
		}
		break; // BLE_GAP_EVT_DISCONNECTED

	case BLE_GATTC_EVT_TIMEOUT:
		// Disconnect on GATT Client timeout event.
		NRF_LOG_DEBUG("GATT Client Timeout.\r\n");
		err_code = sd_ble_gap_disconnect(p_ble_evt->evt.gattc_evt.conn_handle,
		BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
		APP_ERROR_CHECK(err_code);
		break; // BLE_GATTC_EVT_TIMEOUT

	case BLE_GATTS_EVT_TIMEOUT:
		// Disconnect on GATT Server timeout event.
		NRF_LOG_DEBUG("GATT Server Timeout.\r\n");
		err_code = sd_ble_gap_disconnect(p_ble_evt->evt.gatts_evt.conn_handle,
		BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
		APP_ERROR_CHECK(err_code);
		break; // BLE_GATTS_EVT_TIMEOUT

	case BLE_EVT_USER_MEM_REQUEST:
		err_code = sd_ble_user_mem_reply(m_mde.conn_handle, NULL);
		APP_ERROR_CHECK(err_code);
		break; // BLE_EVT_USER_MEM_REQUEST

	case BLE_GATTS_EVT_RW_AUTHORIZE_REQUEST: {
		ble_gatts_evt_rw_authorize_request_t req;
		ble_gatts_rw_authorize_reply_params_t auth_reply;

		req = p_ble_evt->evt.gatts_evt.params.authorize_request;

		if (req.type != BLE_GATTS_AUTHORIZE_TYPE_INVALID) {
			if ((req.request.write.op == BLE_GATTS_OP_PREP_WRITE_REQ)
					|| (req.request.write.op == BLE_GATTS_OP_EXEC_WRITE_REQ_NOW)
					|| (req.request.write.op
							== BLE_GATTS_OP_EXEC_WRITE_REQ_CANCEL)) {
				if (req.type == BLE_GATTS_AUTHORIZE_TYPE_WRITE) {
					auth_reply.type = BLE_GATTS_AUTHORIZE_TYPE_WRITE;
				} else {
					auth_reply.type = BLE_GATTS_AUTHORIZE_TYPE_READ;
				}
				auth_reply.params.write.gatt_status = APP_FEATURE_NOT_SUPPORTED;
				err_code = sd_ble_gatts_rw_authorize_reply(
						p_ble_evt->evt.gatts_evt.conn_handle, &auth_reply);
				APP_ERROR_CHECK(err_code);
			}
		}
	}
		break; // BLE_GATTS_EVT_RW_AUTHORIZE_REQUEST

#if (NRF_SD_BLE_API_VERSION == 3)
		case BLE_GATTS_EVT_EXCHANGE_MTU_REQUEST:
		err_code = sd_ble_gatts_exchange_mtu_reply(p_ble_evt->evt.gatts_evt.conn_handle,
				NRF_BLE_MAX_MTU_SIZE);
		APP_ERROR_CHECK(err_code);
		break; // BLE_GATTS_EVT_EXCHANGE_MTU_REQUEST
#endif
		case BLE_GATTS_EVT_WRITE:{
			ble_gatts_evt_write_t * p_evt_write = &p_ble_evt->evt.gatts_evt.params.write;
			uint16_t value;

			value = uint16_decode (p_evt_write->data);
			NRF_LOG_INFO("on_ble_evt id=0x%04x, hlen=0x%04x uuid=0x%04x op=0x%04hx \r\n",
					                  p_ble_evt->header.evt_id, p_ble_evt->header.evt_len, p_evt_write->uuid.uuid, p_evt_write->op);

			NRF_LOG_INFO("on_ble_evt len=%u data0=0x%04hx data1=0x%04hx 0x%04x \r\n",
								           p_evt_write->len, p_evt_write->data[0], p_evt_write->data[1], value);

			//value = uint16_decode(&p_evt_write->data[0]);
			switch (p_evt_write->uuid.uuid) {
			case BLE_UUID_X_CHARACTERISTC_UUID:
				    //NRF_LOG_INFO("on_ble_evt X uuid=0x%04, len=%u data=0x%04x \r\n",p_evt_write->uuid.uuid, p_evt_write->op, p_evt_write->len, value]);

				break;
			case BLE_UUID_Y_CHARACTERISTC_UUID:

			case BLE_UUID_Z_CHARACTERISTC_UUID:

			case BLE_UUID_YAWR_CHARACTERISTC_UUID:
				break;
			default:
				break;
		} // switch on UUID


		} // BLE_GATTS_EVT_WRITE
		break;

	default:
		// No implementation needed.
		break;
	}
}

/**@brief Function for dispatching a BLE stack event to all modules with a BLE stack event handler.
 *
 * @details This function is called from the scheduler in the main loop after a BLE stack
 *          event has been received.
 *
 * @param[in]   p_ble_evt   Bluetooth stack event.
 */
static void ble_evt_dispatch(ble_evt_t * p_ble_evt) {
	/* The Connection state module has to be fed BLE events in order to function correctly
	 * Remember to call ble_conn_state_on_ble_evt before calling any ble_conns_state_* functions.
	 */

	ble_conn_state_on_ble_evt(p_ble_evt);
	pm_on_ble_evt(p_ble_evt);
	bsp_btn_ble_on_ble_evt(p_ble_evt);
	on_ble_evt(p_ble_evt);
	ble_advertising_on_ble_evt(p_ble_evt);
	ble_conn_params_on_ble_evt(p_ble_evt);
	ble_bas_on_ble_evt(&m_bas, p_ble_evt);
}

/**@brief Function for dispatching a system event to interested modules.
 *
 * @details This function is called from the System event interrupt handler after a system
 *          event has been received.
 *
 * @param[in]   sys_evt   System stack event.
 */
static void sys_evt_dispatch(uint32_t sys_evt) {
	// Dispatch the system event to the fstorage module, where it will be
	// dispatched to the Flash Data Storage (FDS) module.
	fs_sys_event_handler(sys_evt);

	// Dispatch to the Advertising module last, since it will check if there are any
	// pending flash operations in fstorage. Let fstorage process system events first,
	// so that it can report correctly to the Advertising module.
	ble_advertising_on_sys_evt(sys_evt);
}

/**@brief Function for initializing the BLE stack.
 *
 * @details Initializes the SoftDevice and the BLE event interrupt.
 */
static void ble_stack_init(void) {
	uint32_t err_code;

	nrf_clock_lf_cfg_t clock_lf_cfg = NRF_CLOCK_LFCLKSRC;

	// Initialize the SoftDevice handler module.
	SOFTDEVICE_HANDLER_APPSH_INIT(&clock_lf_cfg, true);

	ble_enable_params_t ble_enable_params;
	err_code = softdevice_enable_get_default_config(CENTRAL_LINK_COUNT,	PERIPHERAL_LINK_COUNT, &ble_enable_params);
	APP_ERROR_CHECK(err_code);

	// Check the ram settings against the used number of links
	CHECK_RAM_START_ADDR(CENTRAL_LINK_COUNT, PERIPHERAL_LINK_COUNT);

	// Enable BLE stack.
#if (NRF_SD_BLE_API_VERSION == 3)
	ble_enable_params.gatt_enable_params.att_mtu = NRF_BLE_MAX_MTU_SIZE;
#endif
	err_code = softdevice_enable(&ble_enable_params);
	APP_ERROR_CHECK(err_code);

	// Register with the SoftDevice handler module for BLE events.
	err_code = softdevice_ble_evt_handler_set(ble_evt_dispatch);
	APP_ERROR_CHECK(err_code);

	// Register with the SoftDevice handler module for BLE events.
	err_code = softdevice_sys_evt_handler_set(sys_evt_dispatch);
	APP_ERROR_CHECK(err_code);
}

/**@brief Function for the Peer Manager initialization.
 *
 * @param[in] erase_bonds  Indicates whether bonding information should be cleared from
 *                         persistent storage during initialization of the Peer Manager.
 */
static void peer_manager_init(bool erase_bonds) {
	ble_gap_sec_params_t sec_param;
	ret_code_t err_code;

	err_code = pm_init();
	APP_ERROR_CHECK(err_code);

	if (erase_bonds) {
		err_code = pm_peers_delete();
		APP_ERROR_CHECK(err_code);
	}

	memset(&sec_param, 0, sizeof(ble_gap_sec_params_t));

	// Security parameters to be used for all security procedures.
	sec_param.bond = SEC_PARAM_BOND;
	sec_param.mitm = SEC_PARAM_MITM;
	sec_param.io_caps = SEC_PARAM_IO_CAPABILITIES;
	sec_param.oob = SEC_PARAM_OOB;
	sec_param.keypress = SEC_PARAM_KEYPRESS;
	sec_param.io_caps  = SEC_PARAM_IO_CAPABILITIES;
	sec_param.min_key_size = SEC_PARAM_MIN_KEY_SIZE;
	sec_param.max_key_size = SEC_PARAM_MAX_KEY_SIZE;
	sec_param.kdist_own.enc  = 1;
	sec_param.kdist_own.id   = 1;
	sec_param.kdist_peer.enc = 1;
	sec_param.kdist_peer.id  = 1;

	err_code = pm_sec_params_set(&sec_param);
	APP_ERROR_CHECK(err_code);

	err_code = pm_register(pm_evt_handler);

	APP_ERROR_CHECK(err_code);
}

/**@brief Function for initializing the Advertising functionality.
 */
static void advertising_init(void) {
	uint32_t err_code;
	ble_advdata_t advdata;
	ble_adv_modes_config_t options;

	// Build and set advertising data
	memset(&advdata, 0, sizeof(advdata));

	advdata.name_type = BLE_ADVDATA_FULL_NAME;
	advdata.include_appearance = true;
	advdata.flags = BLE_GAP_ADV_FLAGS_LE_ONLY_LIMITED_DISC_MODE;

	memset(&options, 0, sizeof(options));
	options.ble_adv_whitelist_enabled = true;
	options.ble_adv_directed_enabled = true;
	options.ble_adv_directed_slow_enabled = false;
	options.ble_adv_directed_slow_interval = 0;
	options.ble_adv_directed_slow_timeout = 0;
	options.ble_adv_fast_enabled = true;
	options.ble_adv_fast_interval = APP_ADV_FAST_INTERVAL;
	options.ble_adv_fast_timeout = APP_ADV_FAST_TIMEOUT;
	options.ble_adv_slow_enabled = true;
	options.ble_adv_slow_interval = APP_ADV_SLOW_INTERVAL;
	options.ble_adv_slow_timeout = APP_ADV_SLOW_TIMEOUT;

	err_code = ble_advertising_init(&advdata,
	NULL, &options, on_adv_evt, ble_advertising_error_handler);
	APP_ERROR_CHECK(err_code);
}

/**@brief Function for the Event Scheduler initialization.
 */
static void scheduler_init(void) {
	APP_SCHED_INIT(SCHED_MAX_EVENT_DATA_SIZE, SCHED_QUEUE_SIZE);
}

static void ble_mde_update(ble_mde_t *p_mde, int16_t* x, int16_t* y, int16_t* z)
{
    // Send value if connected and notifying
    if (p_mde->conn_handle != BLE_CONN_HANDLE_INVALID)
    {
        uint16_t      len = sizeof(int16_t);
        ble_gatts_hvx_params_t hvx_params;
        memset(&hvx_params, 0, sizeof(hvx_params));

        hvx_params.handle = p_mde->x_char_handles.value_handle;
        hvx_params.type   = BLE_GATT_HVX_NOTIFICATION;
        hvx_params.offset = 0;
        hvx_params.p_len  = &len;
        hvx_params.p_data = (uint8_t*) x;
        sd_ble_gatts_hvx(p_mde->conn_handle, &hvx_params);

        hvx_params.handle = p_mde->y_char_handles.value_handle;
		hvx_params.type   = BLE_GATT_HVX_NOTIFICATION;
		hvx_params.offset = 0;
		hvx_params.p_len  = &len;
		hvx_params.p_data = (uint8_t*) y;
		sd_ble_gatts_hvx(p_mde->conn_handle, &hvx_params);

		hvx_params.handle = p_mde->z_char_handles.value_handle;
		hvx_params.type   = BLE_GATT_HVX_NOTIFICATION;
		hvx_params.offset = 0;
		hvx_params.p_len  = &len;
		hvx_params.p_data = (uint8_t*) z;
		sd_ble_gatts_hvx(p_mde->conn_handle, &hvx_params);

    }
}

/**@brief Function for handling events from the BSP module.
 *
 * @param[in]   event   Event generated by button press.
 */
static void bsp_event_handler(bsp_event_t event) {
	uint32_t err_code;

	NRF_LOG_INFO("bsp_event_handler %d \r\n", event);

	switch (event) {
	case BSP_EVENT_SLEEP:
		NRF_LOG_INFO("sleep \r\n");
		sleep_mode_enter();
		break;

	case BSP_EVENT_DISCONNECT:
		NRF_LOG_INFO("disconnect \r\n" );
		err_code = sd_ble_gap_disconnect(m_mde.conn_handle,	BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
		if (err_code != NRF_ERROR_INVALID_STATE) {
			APP_ERROR_CHECK(err_code);
		}
		break;

	case BSP_EVENT_WHITELIST_OFF:
		NRF_LOG_INFO("whitelist off \r\n");
		if (m_mde.conn_handle == BLE_CONN_HANDLE_INVALID) {
			err_code = ble_advertising_restart_without_whitelist();
			if (err_code != NRF_ERROR_INVALID_STATE) {
				APP_ERROR_CHECK(err_code);
			}
		}
		break;


	default:
		break;
	}
}

/**@brief Function for initializing buttons and leds.
 *
 * @param[out] p_erase_bonds  Will be true if the clear bonding button was pressed to wake the application up.
 */
static void buttons_leds_init(bool * p_erase_bonds) {
	bsp_event_t startup_event;

	uint32_t err_code = bsp_init(BSP_INIT_LED | BSP_INIT_BUTTONS,
			APP_TIMER_TICKS(100, APP_TIMER_PRESCALER), bsp_event_handler);

	APP_ERROR_CHECK(err_code);

	err_code = bsp_btn_ble_init(NULL, &startup_event);
	APP_ERROR_CHECK(err_code);

	*p_erase_bonds = (startup_event == BSP_EVENT_CLEAR_BONDING_DATA);
}

/**@brief Function for the Power manager.
 */
static void power_manage(void) {
	uint32_t err_code = sd_app_evt_wait();

	APP_ERROR_CHECK(err_code);
}
/**@brief Function for when the euler need to be sent BLE.
 */
static void motiondriver_callback(unsigned char type, long *data,int8_t accuracy, unsigned long timestamp) {


	switch (type) {

//DKW - Commenting Out to Test Accel & Quat
//	case PACKET_DATA_EULER: {
//		int16_t x = inv_q16_to_float(data[0]);
//		int16_t y = inv_q16_to_float(data[1]);
//		int16_t z = inv_q16_to_float(data[2]);
//
//		//if (ble_conn_state_encrypted(m_mde.conn_handle) ) {
//		if (ble_conn_state_status(m_mde.conn_handle) == BLE_CONN_STATUS_CONNECTED) {
//			NRF_LOG_INFO("Euler: [" NRF_LOG_FLOAT_MARKER " " NRF_LOG_FLOAT_MARKER " " NRF_LOG_FLOAT_MARKER "]\r\n", NRF_LOG_FLOAT(x), NRF_LOG_FLOAT(y), NRF_LOG_FLOAT(z));
//			NRF_LOG_INFO("E, 0x%hx, 0x%hx, 0x%hx, 0x%hx\n", accuracy, x, y, z);
//
//			ble_mde_update (&m_mde, &x, &y, &z);
//
//		}
//		break;
//	}

	//DKW - Need to change from Euler to Quats - AND get linear accel

	case PACKET_DATA_QUAT: {

//		if (hal.new_gyro && hal.dmp_on) {
//		            short gyro[3], accel_short[3], sensors;
//		            unsigned char more;
//		            long accel[3], quat[4], temperature;
//
//		}else {}

  //DKW - This works but values are wrong and just rigged w/out sending Quat 'W"

		    int16_t qw = inv_q16_to_float(data[0]);
			int16_t qx = inv_q16_to_float(data[1]);
			int16_t qy = inv_q16_to_float(data[2]);
			int16_t qz = inv_q16_to_float(data[3]);

			//if (ble_conn_state_encrypted(m_mde.conn_handle) ) {
			if (ble_conn_state_status(m_mde.conn_handle) == BLE_CONN_STATUS_CONNECTED) {
				NRF_LOG_INFO("Quat: [" NRF_LOG_FLOAT_MARKER " " NRF_LOG_FLOAT_MARKER " " NRF_LOG_FLOAT_MARKER "]\r\n", NRF_LOG_FLOAT(qx), NRF_LOG_FLOAT(qy), NRF_LOG_FLOAT(qz));
				NRF_LOG_INFO("E, 0x%hx, 0x%hx, 0x%hx, 0x%hx, 0x%hx\n", accuracy, qw, qx, qy, qz);

				ble_mde_update (&m_mde, &qx, &qy, &qz); //DKW - Add qw


		}
			break;
		}

	//DKW - Added - Also need to add LINEAR Acceleration
//	case PACKET_DATA_ACCEL: {
//			int16_t ax = inv_q16_to_float(data[0]);
//			int16_t ay = inv_q16_to_float(data[1]);
//			int16_t az = inv_q16_to_float(data[2]);
//
//			//if (ble_conn_state_encrypted(m_mde.conn_handle) ) {
//			if (ble_conn_state_status(m_mde.conn_handle) == BLE_CONN_STATUS_CONNECTED) {
//				NRF_LOG_INFO("Accel: [" NRF_LOG_FLOAT_MARKER " " NRF_LOG_FLOAT_MARKER " " NRF_LOG_FLOAT_MARKER "]\r\n", NRF_LOG_FLOAT(x), NRF_LOG_FLOAT(y), NRF_LOG_FLOAT(z));
//				NRF_LOG_INFO("E, 0x%hx, 0x%hx, 0x%hx, 0x%hx\n", accuracy, ax, ay, az);
//
//				ble_mde_update (&m_mde, &ax, &ay, &az);
//
//			}
//			break;
//		}

	default:
		break;

	}
}

void app_error_fault_handler(uint32_t id, uint32_t pc, uint32_t info) {
	NRF_LOG_ERROR("Fatal: %d %d %d\r\n", id, pc, info)
	NRF_LOG_FINAL_FLUSH()
	;
	// On assert, the system can only recover with a reset.
#ifndef DEBUG
	NVIC_SystemReset();
#else
	app_error_save_and_stop(id, pc, info);
#endif // DEBUG
}
#ifdef xNRF_LOG_BACKEND_SERIAL_USES_UART
 void uart_error_handle(app_uart_evt_t * p_event)
 {
     if (p_event->evt_type == APP_UART_COMMUNICATION_ERROR)
     {
         APP_ERROR_HANDLER(p_event->data.error_communication);
     }
     else if (p_event->evt_type == APP_UART_FIFO_ERROR)
     {
         APP_ERROR_HANDLER(p_event->data.error_code);
     }
 }

 static void uart_init(void)
 {
     uint32_t err_code;

     app_uart_comm_params_t const comm_params = {
         RX_PIN_NUMBER,
         TX_PIN_NUMBER,
         RTS_PIN_NUMBER,
         CTS_PIN_NUMBER,
         APP_UART_FLOW_CONTROL_ENABLED,
         false,
         UART_BAUDRATE_BAUDRATE_Baud115200
     };

     APP_UART_FIFO_INIT(&comm_params,
                          UART_RX_BUF_SIZE,
                          UART_TX_BUF_SIZE,
                          uart_error_handle,
                          APP_IRQ_PRIORITY_LOW,
                          err_code);

     APP_ERROR_CHECK(err_code);
 }
#endif
/**@brief Initialize the TWI interface from the NRF52 to the MPU9260.
 */
static void twi_init(void) {
	uint32_t err_code;

	nrf_drv_twi_config_t const twi_config = {
			.scl = ARDUINO_SCL_PIN,
			.sda = ARDUINO_SDA_PIN,
			.frequency = NRF_TWI_FREQ_400K,
			.interrupt_priority = APP_IRQ_PRIORITY_HIGH };

	APP_TWI_INIT(&m_app_twi, &twi_config, TWI_MAX_PENDING_TRANSACTIONS,
			err_code);
	APP_ERROR_CHECK(err_code);
}
/**@brief Function to initialize and test the md612 drivers.
 */
static void motiondriver_init(void) {
	NRF_LOG_INFO("Motion Driver init...\r\n");

	md612_configure(&platform_data);
	md612_selftest();

}
/**@brief Function for application main entry.
 */
int main(void) {
	bool erase_bonds;
	uint32_t err_code;

	// Initialize.
	err_code = NRF_LOG_INIT(timestamp_func);
	APP_ERROR_CHECK(err_code);

	timers_init();
	buttons_leds_init(&erase_bonds);
	twi_init();
	nrf_delay_ms(10);

	motiondriver_init();
	ble_stack_init();

	scheduler_init();

	peer_manager_init(erase_bonds);

	if (erase_bonds == true) {
		NRF_LOG_INFO("Bonds erased!\r\n");
	}

	gap_params_init();
	advertising_init();
	services_init();
	sensor_simulator_init();
	conn_params_init();

	// Start execution.
	timers_start();

	advertising_start();

	NRF_LOG_INFO("Enter main loop\r\n");
	// Enter main loop.
	for (;;) {
		app_sched_execute();

		md612_beforesleep();

		if (NRF_LOG_PROCESS() == false && md612_hasnewdata() == false) {
			power_manage();
			continue;
		}

		md612_aftersleep();

	}  // for loop

} // Main
/**
 * @}
 */

