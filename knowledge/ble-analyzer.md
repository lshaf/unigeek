# BLE Analyzer

Accessed from **Bluetooth > BLE Analyzer**.

Scans for nearby Bluetooth Low Energy devices and displays a detailed info view for each one, including vendor identification, signal strength, estimated distance, and raw advertisement data.

## Scanning

Tap **Scan** to start a 5-second passive BLE scan. Results appear as a list showing each device's name (or address if unnamed) and RSSI. Tap any entry to open its info view.

## Info View

The info view shows up to 16 fields extracted from the advertisement:

| Field | Description |
|-------|-------------|
| **Name** | Advertised local name; replaced by vendor lookup if unnamed |
| **Vendor** | Company name from the Bluetooth SIG Assigned Numbers database |
| **Address** | 6-byte MAC address |
| **Addr Type** | Public, random static, resolvable private, or non-resolvable private |
| **Appearance** | Declared device category (e.g. Phone, Watch, HID) |
| **RSSI** | Received signal strength in dBm |
| **TX Power** | Declared transmit power in dBm (if advertised) |
| **Distance** | Rough path-loss estimate in metres using TX power and RSSI |
| **Adv Type** | Advertisement PDU type (e.g. ADV\_IND, ADV\_NONCONN\_IND) |
| **Adv Flags** | LE General/Limited Discoverable, BR/EDR flags |
| **Connectable** | Yes / No |
| **Svc UUIDs** | Count of advertised service UUIDs |
| **Mfr Data** | Count of manufacturer-specific data records |
| **Svc Data** | Count of service data records |
| **URI** | Advertised URI (if present) |
| **Payload** | Total raw advertisement payload size in bytes |

Fields with no data are shown as empty and are not tappable.

## Detail View

Tapping a field that has data (Svc UUIDs, Mfr Data, Svc Data, or any other populated row) opens a full-screen scrollable detail view showing the raw content — UUID strings, hex-encoded manufacturer data, or service data blobs.

Press **BACK** to return to the info view, or **BACK** again to return to the scan list.

## Achievements

| Achievement | Tier |
|------------|------|
| **Bluetooth Scout** | Bronze |
| **BLE Inspector** | Bronze |
| **BLE Census** | Silver |