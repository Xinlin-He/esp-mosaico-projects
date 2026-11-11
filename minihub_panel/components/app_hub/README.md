# app_hub

Owns the Mosaico panel's MiniHub HTTP session, Point cache, and command queue.
It does not own the Wi-Fi station. Its worker may perform a bounded, read-only
scan through the `esp_wifi` instance already initialized by official MiniHub.
`app_hub_start()` creates one process-lifetime instance.

The worker task is the sole owner of HTTP and authentication state. UI callers
only copy snapshots or enqueue commands; they never block on network I/O.
Mutable Point data is protected by an internal mutex. The public snapshot has
fixed-size value fields and contains no borrowed pointers.

Hub mode always targets `http://127.0.0.1`. Panel mode persists a remote base
URL and bearer token in this component's NVS namespace; changing modes clears
the HTTP cookie/cache state and wakes the worker. Credentials are not logged.
Selected Wi-Fi credentials are submitted only to official MiniHub's
`POST /api/net/wifi`; this component does not persist Wi-Fi passwords or call
`esp_wifi_connect`.

The component also keeps low-frequency scene, automation-rule, and lightweight
system snapshots. Scene execution, rule enable changes, and restart requests
are serialized through the worker queue; the UI never performs HTTP inline.
Rule enable changes preserve and re-import the complete rules document because
MiniHub does not expose a single-rule patch endpoint.

`app_hub_add_device()` accepts Matter, Zigbee, BLE Mesh, MQTT, and Modbus TCP
requests. Availability is reported by `app_hub_add_snapshot().capabilities`;
Zigbee and Thread are separate firmware variants, while Zigbee and BLE Mesh
switch the shared 2.4 GHz radio at runtime. Matter first-time pairing uses
`ble_wifi` and injects the live STA SSID/password (also copied into MiniHub
provision NVS). On GOT_IP the worker applies MiniHub's STA default-netif and
DNS backup so CHIP FindOperational can browse `_matter._tcp`. Wi-Fi modem
sleep stays off for BLE discovery. A Matter add uses the same
`CONFIG_MINIHUB_MATTER_COMMISSION_WAIT_MS` deadline as the Host (300 s). The worker
preserves unknown adapter configuration fields, waits for the resulting Point,
applies the requested display name, and reports timeout/cancellation through
the add snapshot. MQTT and Modbus entries are removed again if activation
times out.

Scene and rule creation use bounded value-copy arguments. Scene creation captures
at most four Point targets; rule creation uses one Point equality trigger and
one power action. API calls enqueue work and return immediately. Snapshot and
enqueue methods are thread-safe; callers must copy any transient input strings
before returning, as the component does internally.

Store management, advanced rule/scene editing, network administration, and
firmware updates remain Web-only. Mosaico application updates continue through
Vibe Mode and Iris rather than MiniHub's generic OTA API.
