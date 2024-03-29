#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "nvs_flash.h"

#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_config_model_api.h"
#include "esp_ble_mesh_generic_model_api.h"
#include "esp_ble_mesh_lighting_model_api.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_provisioning_api.h"

#include "esp_gatt_common_api.h"

#include "ble_mesh_init.h"
#include "ble_mesh_nvs.h"
#include "button.h"

#define TAG "NODE_SWITCH"

#define CID_ESP 0x02E5

static uint8_t dev_uuid[16] = {0xdd, 0xdd};

// These informations do not get automatically stored
// by the BLE Mesh stack, so we store them manually
static struct ble_mesh_info_store_t {
  uint8_t tid;
  uint8_t onoff;
} __attribute__((packed)) ble_mesh_info_store = {
    .tid = 0x0,
    .onoff = 0x0,
};
static nvs_handle_t NVS_HANDLE;
static const char *NVS_KEY = "onoff_client";

static esp_ble_mesh_cfg_srv_t config_server = {
    .relay = ESP_BLE_MESH_RELAY_DISABLED,
    .beacon = ESP_BLE_MESH_BEACON_ENABLED,
    .friend_state = ESP_BLE_MESH_FRIEND_NOT_SUPPORTED,
    .gatt_proxy = ESP_BLE_MESH_GATT_PROXY_ENABLED,
    .default_ttl = 7,
    /* 3 transmissions with 20ms interval */
    .net_transmit = ESP_BLE_MESH_TRANSMIT(2, 20),
    .relay_retransmit = ESP_BLE_MESH_TRANSMIT(2, 20),
};

static esp_ble_mesh_client_t onoff_client;
ESP_BLE_MESH_MODEL_PUB_DEFINE(onoff_cli_pub, 2 + 3, ROLE_NODE);

static esp_ble_mesh_client_t lightness_client;
ESP_BLE_MESH_MODEL_PUB_DEFINE(lightness_cli_pub, 2 + 3, ROLE_NODE);

static esp_ble_mesh_model_t root_models[] = {
    ESP_BLE_MESH_MODEL_CFG_SRV(&config_server),
    ESP_BLE_MESH_MODEL_GEN_ONOFF_CLI(&onoff_cli_pub, &onoff_client),
    ESP_BLE_MESH_MODEL_LIGHT_LIGHTNESS_CLI(&lightness_cli_pub, &lightness_client),
};

static esp_ble_mesh_elem_t elements[] = {
    ESP_BLE_MESH_ELEMENT(0, root_models, ESP_BLE_MESH_MODEL_NONE),
};

static esp_ble_mesh_comp_t composition = {
    .cid = CID_ESP,
    .elements = elements,
    .element_count = ARRAY_SIZE(elements),
};

static esp_ble_mesh_prov_t provision = {
    .uuid = dev_uuid,
    // No OOB
    .output_size = 0,
    .output_actions = 0,
};

static void mesh_info_store(void) {
  ble_mesh_nvs_store(NVS_HANDLE, NVS_KEY, &ble_mesh_info_store, sizeof(ble_mesh_info_store));
}

static void mesh_info_restore(void) {
  esp_err_t err = ESP_OK;
  bool exist = false;

  err = ble_mesh_nvs_restore(NVS_HANDLE, NVS_KEY, &ble_mesh_info_store, sizeof(ble_mesh_info_store), &exist);
  if (err != ESP_OK) {
    return;
  }

  if (exist) {
    ESP_LOGI(TAG, "Restore, tid 0x%02x, onoff 0x%04x", ble_mesh_info_store.tid, ble_mesh_info_store.onoff);
  }
}

static void prov_complete(uint16_t net_idx, uint16_t addr, uint8_t flags, uint32_t iv_index) {
  ESP_LOGI(TAG, "net_idx: 0x%04x, addr: 0x%04x", net_idx, addr);
  ESP_LOGI(TAG, "flags: 0x%02x, iv_index: 0x%08x", flags, iv_index);
}

static void ble_mesh_provisioning_cb(esp_ble_mesh_prov_cb_event_t event, esp_ble_mesh_prov_cb_param_t *param) {
  switch (event) {
  case ESP_BLE_MESH_PROV_REGISTER_COMP_EVT:
    ESP_LOGI(TAG, "ESP_BLE_MESH_PROV_REGISTER_COMP_EVT, err_code %d", param->prov_register_comp.err_code);
    mesh_info_restore(); /* Restore proper mesh example info */
    break;
  case ESP_BLE_MESH_NODE_PROV_ENABLE_COMP_EVT:
    ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_PROV_ENABLE_COMP_EVT, err_code %d", param->node_prov_enable_comp.err_code);
    break;
  case ESP_BLE_MESH_NODE_PROV_LINK_OPEN_EVT:
    ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_PROV_LINK_OPEN_EVT, bearer %s",
             param->node_prov_link_open.bearer == ESP_BLE_MESH_PROV_ADV ? "PB-ADV" : "PB-GATT");
    break;
  case ESP_BLE_MESH_NODE_PROV_LINK_CLOSE_EVT:
    ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_PROV_LINK_CLOSE_EVT, bearer %s",
             param->node_prov_link_close.bearer == ESP_BLE_MESH_PROV_ADV ? "PB-ADV" : "PB-GATT");
    break;
  case ESP_BLE_MESH_NODE_PROV_COMPLETE_EVT:
    ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_PROV_COMPLETE_EVT");
    prov_complete(param->node_prov_complete.net_idx, param->node_prov_complete.addr, param->node_prov_complete.flags,
                  param->node_prov_complete.iv_index);
    break;
  case ESP_BLE_MESH_NODE_PROV_RESET_EVT:
    break;
  case ESP_BLE_MESH_NODE_SET_UNPROV_DEV_NAME_COMP_EVT:
    ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_SET_UNPROV_DEV_NAME_COMP_EVT, err_code %d",
             param->node_set_unprov_dev_name_comp.err_code);
    break;
  default:
    break;
  }
}

void ble_mesh_send_gen_onoff_set(void) {
  // message: first 8 bits -> tid
  //          last  8 bits -> status
  ble_mesh_info_store.tid++;
  uint16_t value_to_send = ((uint16_t)ble_mesh_info_store.tid << 8) | ble_mesh_info_store.onoff;

  ESP_LOGI(TAG, "Sending message");
  esp_err_t err = esp_ble_mesh_model_publish(onoff_client.model, ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET_UNACK,
                                             sizeof(value_to_send), (uint8_t *)&value_to_send, ROLE_NODE);
  if (err) {
    ESP_LOGE(TAG, "Send Generic OnOff Set Unack failed");
    return;
  }

  ble_mesh_info_store.onoff = !ble_mesh_info_store.onoff;

  mesh_info_store();
}

static void ble_mesh_generic_client_cb(esp_ble_mesh_generic_client_cb_event_t event,
                                       esp_ble_mesh_generic_client_cb_param_t *param) {
  ESP_LOGI(TAG, "Generic client, event %u, error code %d, opcode is 0x%04x", event, param->error_code,
           param->params->opcode);

  switch (event) {
  case ESP_BLE_MESH_GENERIC_CLIENT_GET_STATE_EVT:
    ESP_LOGI(TAG, "ESP_BLE_MESH_GENERIC_CLIENT_GET_STATE_EVT");
    if (param->params->opcode == ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_GET) {
      ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_GET, onoff %d", param->status_cb.onoff_status.present_onoff);
    }
    break;
  case ESP_BLE_MESH_GENERIC_CLIENT_SET_STATE_EVT:
    ESP_LOGI(TAG, "ESP_BLE_MESH_GENERIC_CLIENT_SET_STATE_EVT");
    if (param->params->opcode == ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET) {
      ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET, onoff %d", param->status_cb.onoff_status.present_onoff);
    }
    break;
  case ESP_BLE_MESH_GENERIC_CLIENT_PUBLISH_EVT:
    ESP_LOGI(TAG, "ESP_BLE_MESH_GENERIC_CLIENT_PUBLISH_EVT");
    break;
  case ESP_BLE_MESH_GENERIC_CLIENT_TIMEOUT_EVT:
    ESP_LOGI(TAG, "ESP_BLE_MESH_GENERIC_CLIENT_TIMEOUT_EVT");
    if (param->params->opcode == ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET) {
      /* If failed to get the response of Generic OnOff Set, resend Generic OnOff Set  */
      ble_mesh_send_gen_onoff_set();
    }
    break;
  default:
    break;
  }
}

static void ble_mesh_config_server_cb(esp_ble_mesh_cfg_server_cb_event_t event,
                                      esp_ble_mesh_cfg_server_cb_param_t *param) {
  if (event == ESP_BLE_MESH_CFG_SERVER_STATE_CHANGE_EVT) {
    switch (param->ctx.recv_op) {
    case ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD:
      ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD");
      ESP_LOGI(TAG, "net_idx 0x%04x, app_idx 0x%04x", param->value.state_change.appkey_add.net_idx,
               param->value.state_change.appkey_add.app_idx);
      ESP_LOG_BUFFER_HEX("AppKey", param->value.state_change.appkey_add.app_key, 16);
      break;
    case ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND:
      ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND");
      ESP_LOGI(TAG, "elem_addr 0x%04x, app_idx 0x%04x, cid 0x%04x, mod_id 0x%04x",
               param->value.state_change.mod_app_bind.element_addr, param->value.state_change.mod_app_bind.app_idx,
               param->value.state_change.mod_app_bind.company_id, param->value.state_change.mod_app_bind.model_id);
      if (param->value.state_change.mod_app_bind.company_id == 0xFFFF &&
          param->value.state_change.mod_app_bind.model_id == ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_CLI) {
      }
      break;
    case ESP_BLE_MESH_MODEL_OP_MODEL_SUB_ADD:
      ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_MODEL_SUB_ADD");
      ESP_LOGI(TAG, "elem_addr 0x%04x, sub_addr 0x%04x, cid 0x%04x, mod_id 0x%04x",
               param->value.state_change.mod_sub_add.element_addr, param->value.state_change.mod_sub_add.sub_addr,
               param->value.state_change.mod_sub_add.company_id, param->value.state_change.mod_sub_add.model_id);
      ble_mesh_send_gen_onoff_set();
      break;
    case ESP_BLE_MESH_MODEL_OP_MODEL_PUB_SET:
      ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_MODEL_PUB_SET");
      ESP_LOGI(TAG, "elem_addr 0x%04x, sub_addr 0x%04x, cid 0x%04x, mod_id 0x%04x",
               param->value.state_change.mod_sub_add.element_addr, param->value.state_change.mod_sub_add.sub_addr,
               param->value.state_change.mod_sub_add.company_id, param->value.state_change.mod_sub_add.model_id);
      break;
    case ESP_BLE_MESH_MODEL_OP_MODEL_SUB_DELETE:
      ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_MODEL_SUB_DELETE");
      ESP_LOGI(TAG, "elem_addr 0x%04x, del_addr 0x%04x, cid 0x%04x, mod_id 0x%04x",
               param->value.state_change.mod_sub_add.element_addr, param->value.state_change.mod_sub_add.sub_addr,
               param->value.state_change.mod_sub_add.company_id, param->value.state_change.mod_sub_add.model_id);

    default:
      break;
    }
  }
}

static esp_err_t ble_mesh_init(void) {
  esp_err_t err = ESP_OK;

  esp_ble_mesh_register_prov_callback(ble_mesh_provisioning_cb);
  esp_ble_mesh_register_generic_client_callback(ble_mesh_generic_client_cb);
  esp_ble_mesh_register_config_server_callback(ble_mesh_config_server_cb);

  err = esp_ble_mesh_init(&provision, &composition);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize mesh stack (err %d)", err);
    return err;
  }

  err = esp_ble_mesh_node_prov_enable(ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to enable mesh node (err %d)", err);
    return err;
  }

  ESP_LOGI(TAG, "BLE Mesh Node initialized");

  return err;
}

void app_main(void) {
  esp_err_t err;

  ESP_LOGI(TAG, "Initializing...");

  err = nvs_flash_init();
  nvs_flash_erase();
  err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);

  err = bluetooth_init();
  if (err) {
    ESP_LOGE(TAG, "esp32_bluetooth_init failed (err %d)", err);
    return;
  }

  /* Open nvs namespace for storing/restoring ble mesh info */
  err = ble_mesh_nvs_open(&NVS_HANDLE);
  if (err) {
    return;
  }

  ble_mesh_get_dev_uuid(dev_uuid);

  /* Initialize the Bluetooth Mesh Subsystem */
  err = ble_mesh_init();
  if (err) {
    ESP_LOGE(TAG, "Bluetooth mesh init failed (err %d)", err);
  }

  esp_ble_gatt_set_local_mtu(120);

  board_button_init();
}
