# esp32s3-netlab

A home network lab on an ESP32-S3: a web page served by the chip itself, backed by a small JSON API.
The end goal is to watch your home network and internet connection, analyze Wi-Fi, and measure how
much speed you lose if the S3 sits between your devices and the router.

Milestone 1 (this state of the project) has no external sensors: it shows the chip temperature,
the Wi-Fi link to your router, and lets you drive the onboard RGB LED.

**The firmware configures itself.** There are no Wi-Fi credentials in the code or the build: the same
firmware image works in any home. On first boot (or in a new place) the device opens its own Wi-Fi,
you pick your router from your phone, and it remembers it. No rebuild, no reflash.

## How it works

Boot sequence (`main/app_main.c`):

1. Initialize NVS, the network stack and the default event loop.
2. Turn the status LED on (blue) and start the chip temperature sensor, the heap monitor and the reset button.
3. Start mDNS (`netlab.local`) and mount the `www` LittleFS partition that holds the web UI.
4. Start Wi-Fi (`wifi_bridge`):
   - **A network is saved in NVS:** join it (amber, then green once it has an IP). If the link is lost it
     retries every 2 s, forever.
   - **Nothing saved:** open the setup network (purple LED) and wait for the user.
5. Start the HTTP server: the JSON API lives under `/api/v1/*`, every other GET is served from `www`
   (or the setup page while the setup network is open).

| Part | Role |
|---|---|
| `components/wifi_bridge` | Wi-Fi manager: saved network in NVS, reconnect, setup network, captive-portal DNS, provisioning, scan. Optional [router lab mode](#router-lab-mode-classroom): a second Wi-Fi network routed through NAT, with flow and DNS metadata (`traffic_observer.c`). |
| `components/chip_health` | Internal temperature sensor, heap, uptime, reset reason. |
| `components/heap_monitor` | Runs [heaptop](https://components.espressif.com/components/pedroluisdionisiofraga/heaptop) and renders its `ht` console views as text for the `/heaptop` page. |
| `components/notification_manager` | Owns the onboard LED. One worker task is the only LED writer; `wifi_bridge` sets a `NOTIF_EVT_CONN_*` event bit and the worker shows the matching status color. The web UI's manual color overrides it. |
| `components/led_device` | Hardware layer for the WS2812 (set color, off, brightness cap). Only `notification_manager` uses it. |
| `components/rest_server` | HTTP server: JSON API, the built-in Wi-Fi setup page, static files from LittleFS. |
| `main/reset_button.c` | Hold BOOT for 5 s to forget the saved network. |
| `front/web` | The Vue + Vite dashboard, built into `front/web/dist` and flashed to the `www` partition. |

## Hardware

- ESP32-S3-DevKitC-1 **v1.1** (RGB LED on GPIO38). On a **v1.0** board the LED is on GPIO48:
  change it in `menuconfig` -> *Status LED*.
- The temperature is the **die temperature of the chip**, not the room temperature.
- The ESP32-S3 is **2.4 GHz only**: it cannot join a 5 GHz-only network.

## Requirements

- **ESP-IDF v6.0.2**, installed **with the `esp32s3` target** (the manifest in `main/idf_component.yml`
  refuses older versions).
- To build the web UI, either **Docker** (no Node.js needed, uses `build.sh` / `build.ps1`) or
  Node.js and pnpm.

## Build and flash

### 1. Build the web UI

Output goes to `front/web/dist`, which is flashed into the `www` LittleFS partition. Pick one:

**Docker (recommended).** Run from the repo root. The script starts Docker Desktop if it is not running.

```powershell
.\build.ps1          # PowerShell
.\build.ps1 -Clean   # delete dist, rebuild the image without cache
```

```sh
./build.sh           # Git Bash, macOS, Linux
./build.sh --clean   # delete dist, rebuild the image without cache
```

**Local Node.js.**

```powershell
cd front/web
pnpm install
pnpm build
cd ../..
```

### 2. Build, flash, monitor

Run from an ESP-IDF shell, in the repo root.

```powershell
idf.py set-target esp32s3
idf.py -p COMx build flash monitor
```

No credentials are needed at build time. Rebuild the web UI (step 1) whenever you change
something in `front/web`, then flash again.

### Working on the web UI without reflashing

In `menuconfig` disable *Netlab* -> *Flash the web UI*, flash once, then:

```powershell
cd front/web
$env:ESP_HOST = "http://<device-ip>"
pnpm dev
```

The Vite dev server proxies `/api` to the device.

## First use: connecting it to your Wi-Fi

1. Power the board. With no saved network the LED turns **purple** and a Wi-Fi network called
   **`Netlab-Fraga`** appears (the name can be changed in `menuconfig` -> *Wi-Fi bridge*).
2. On your phone or laptop join it. The default password is **`netlabsetup`**
   (change it in `menuconfig` -> *Wi-Fi bridge*).
3. The setup page should open by itself (captive portal). If not, open **http://192.168.4.1/**.
4. Pick your router in the list (or type its name for a hidden network), enter the password, press
   **Connect**.
5. The device tries the network. On success it **saves it to NVS**, the LED turns green, the page says
   *Connected*, and the setup network closes a few seconds later.
6. Put your phone back on your home Wi-Fi and open **http://netlab.local/** (or the IP address shown in the
   serial log as `Got IP`).

A wrong password or an out-of-range router is reported on the page and **nothing is saved**, so a typo
cannot lock the device out of a network that worked.

### Moving it to another home (or changing the router)

- **Automatic:** if the saved network cannot be reached for 90 s (for example you plugged it in at your
  parents' house), the setup network opens again next to the retries. Connect to it and choose the new
  network. When the device is online again the setup network closes on its own.
- **From the dashboard:** *Wi-Fi link* card -> **Change network…** forgets the saved network and restarts
  into setup mode.
- **From the board:** hold the **BOOT** button for 5 seconds after the board has booted. The LED turns
  white and the device restarts into setup mode.

Notes:

- While the setup network is open, the device stops retrying the saved router as long as someone is
  connected to the setup network, so the page stays responsive.
- Joining your router can make the setup network change radio channel for a moment. The page tolerates a
  few failed requests and tells you what to do if it loses contact.
- The setup network normally only lasts until the device is online. If the saved router is just rebooting
  (a power cut), the setup network appears after 90 s and disappears by itself when the router is back.

### Where the credentials are stored

In **NVS** (flash), namespace `netlab`, keys `wifi_ssid` and `wifi_pass`. They are never in the firmware
image, the repository, or the build files. NVS is not encrypted by default: anyone with physical access
and a flasher can read it. Enable NVS or flash encryption if that matters to you.

## Using the web page

Open `http://netlab.local/` from a device on the same network.

- **Header badge:** *Device reachable* while the page gets answers, *Device unreachable* otherwise.
- **Chip:** die temperature (turns amber from 70 °C), uptime, free heap, lowest free heap, last reset
  reason, chip and ESP-IDF version.
- **Wi-Fi link:** signal quality from the RSSI, the router's network name, channel and BSSID, the IP
  address, gateway and netmask the S3 got, and the **Change network…** button.
- **Onboard LED:** pick a color, press **Off**, or press **Auto** to go back to the status colors.

### Heaptop page

Open `http://netlab.local/heaptop` (or **Heaptop** in the header). It shows the memory and CPU monitor
[heaptop](https://github.com/PedroLuisDionisioFraga/esp32s3_heaptop) with the same text its `ht` serial
command prints: the device renders it with heaptop's own code, so the columns match the heaptop README.

- **Top / Heap / Tasks / Health:** `ht top` (live view), `ht heap`, `ht tasks <sort>`, `ht health`.
- **Sort** (Top and Tasks), **Pause**, **−/+** (50 ms steps up to 200 ms, 200 ms up to 3 s, 500 ms up to 5 s, then 1 s; 50 ms to 10 s): the keys of
  `ht top` work too: `c` `m` `s` `n` sort by CPU/memory/stack/name, `p` pauses, `+`/`-` change the refresh.
  As in `ht top`, pausing freezes the sample on screen, and a new view or sort while paused redraws that
  same sample. The device keeps one frozen copy for all browsers, so another tab that is not paused moves
  it on.
- **Clear stats:** like `ht clear`. Min free, task peaks, failures, trends and the leak check start over.

What each number means is in the heaptop README (*Heap basics in one minute*, *Health checks*). Memory is
charged to the task that allocated it, so the page's own requests show up under `httpd`.

## Router lab mode (classroom)

Off by default. When enabled, the S3 becomes a small router for an **authorized classroom lab**: once it
is online through the saved network (the *uplink*), it opens a second WPA2 network, the **lab network**,
gives its clients addresses by DHCP and routes them to the uplink through IPv4 NAT. The **Router lab**
page (`http://netlab.local/router`) shows the lab clients and, only while **capture** is on, which
addresses, ports and DNS names they reach.

```text
 internet ── lab router / AP (uplink, saved network) ── S3 ── lab network "Netlab-Lab" ── student devices
                                                        └─ dashboard: http://netlab.local/router (uplink side)
```

### Rules

- Use it only on a network you run, with devices whose owners agreed to be observed. Say when capture is on.
- The lab network has **its own name**. It never takes the name of the uplink or of any other real network:
  it refuses to open when its name equals the uplink's, and it is always WPA2-protected.
- **What is recorded** (RAM only, gone at reboot or *Clear*): per flow the client address, destination
  address, destination port, protocol, packet and byte counts (client to destination) and when it was
  seen; the most recent DNS names asked; requests sent to the lab service. The tables are bounded
  (64 flows and 16 DNS names by default): when the flow table is full, the flow seen least recently is
  dropped (*Flows dropped*). Only the two totals (packets and bytes of all lab clients together) are
  counted with capture off.
- **What is never recorded:** packet payloads, passwords, cookies, page contents. HTTPS traffic stays
  encrypted: the S3 only forwards it and sees the destination address and port, never what is inside.
  There is no DNS hijacking, no captive portal and no TLS interception on the lab network.
- The **lab service** (`POST /api/v1/router/lab`) is the class's own plain-HTTP server: the page shows the
  start of each request body it received, to show what any HTTP server (and anyone on the path, without
  TLS) can read. Only while capture is on. Never send a real password to it.
- Capture can only be started, stopped or cleared from the **uplink side** (`403` from lab clients).
  That is a convenience, not security: the dashboard has no login (see [Notes](#notes)).

### Setup

1. `idf.py menuconfig` -> *Wi-Fi bridge* -> **Router lab mode**. Set the lab network name and password
   (8 to 63 characters). The defaults are `Netlab-Lab` / `netlablab`: change the password.
2. If the uplink already uses `192.168.4.x`, set *Lab network address* to another /24 (for example
   `192.168.42.1`). The setup network moves to that address too, since both use the same soft AP.
3. Build and flash (`idf.py -p COMx build flash monitor`). The forwarding options (`LWIP_IP_FORWARD`,
   `LWIP_IPV4_NAPT`) are selected automatically.
4. Provision the uplink as usual. Once online, the log prints `Lab network 'Netlab-Lab' is open and routed
   to the uplink`. Join it from the student devices.
5. Open the Router lab page from a computer on the uplink network and press **Start capture** when the class
   is ready. **Export JSON** downloads everything shown on the page.

**Rollback:** disable *Router lab mode* in `menuconfig` and flash again. The firmware is then exactly the
dashboard and setup flow described above; the saved network is kept.

### How it behaves

- The lab network exists only while the uplink is up. When the uplink is lost, the lab network closes at
  once (its clients see the network disappear, so nobody keeps a stale address with no internet), and the
  device retries and opens the setup network as usual. It reopens when the uplink is back.
- **One radio:** the lab network always sits on the uplink's channel, and every packet crosses the air
  twice. Expect a few Mbit/s at best, less with several clients: the counters are there to show the cost,
  not to promise router speed. Up to 4 clients by default (8 at most).
- Only IPv4 is routed. Byte counts are client-to-destination only: replies are not counted.

### Classroom exercises

1. **What a router sees.** Capture on, students browse a few sites. Compare the DNS names and destination
   addresses with what they visited. Which sites can be told apart only by address?
2. **HTTP vs HTTPS.** Send text to the lab service from the page, or `curl -X POST
   http://192.168.4.1/api/v1/router/lab -d 'hello'` from a lab client: the page shows the text. Then open an
   HTTPS site and find its flow: only the address and port 443 are visible.
3. **NAT.** Compare a client's lab address (`192.168.4.x`) with the address the uplink router sees for all
   of them (the S3's own address in the *Wi-Fi link* card).
4. **Cost of a hop.** Run a speed test on the uplink and on the lab network, with one and with three clients.

### Lab troubleshooting

| Symptom | What to check |
|---|---|
| Page says *The uplink uses the lab network's subnet* | Change *Lab network address* to a /24 the uplink does not use, then flash again. |
| Page says *The lab network name is the uplink's name* | Choose a different *Lab network name*. |
| Clients join but get no address | The log should show the DHCP server starting. Too many clients: raise *Maximum devices on the lab network* (8 at most). |
| Clients get an address but no internet | The log says `NAT did not start`, or the uplink is down. Check the *Wi-Fi link* card. |
| Addresses work, names do not | The uplink gave no DNS server (`The uplink gave no DNS server` in the log). Fix the uplink's DHCP. |
| Lab network vanished for a moment | The uplink dropped or changed channel. The lab network follows the uplink and comes back on its own. |
| A student device keeps using the uplink | It also knows the uplink network. Forget the uplink on that device so it only joins `Netlab-Lab`. |
| Capture buttons answer `403` | You are on the lab network. Use a computer on the uplink network. |

### Checks

The packet parser has a host test (needs a C compiler on the PC), from the repo root:

```sh
gcc -Wall -Wextra -I components/wifi_bridge components/wifi_bridge/host_test/test_traffic_parse.c \
    components/wifi_bridge/traffic_parse.c -o test_traffic_parse && ./test_traffic_parse
```

On hardware: build with the mode off and on; join the lab network from two devices and check the lease,
DNS and an HTTP site through the NAT; check that flows appear only with capture on and stop at once when it
is turned off; unplug the uplink router and check that the lab network closes and the setup network opens
after the usual delay; plug it back and check that the lab network returns.

## LED colors

| Color | Meaning |
|---|---|
| Blue | booting |
| Purple | setup network is open: connect to `Netlab-Fraga` |
| Amber | connecting to the router |
| Green | online (has an IP address) |
| Red | offline, reconnecting |
| White | resetting the Wi-Fi network (BOOT held) |

A color picked in the web UI overrides this until you press **Auto**. Brightness is capped at 20 % by
default (`menuconfig` -> *Status LED*), because a WS2812 at full power is blinding.

## API

| Endpoint | Method | Description |
|---|---|---|
| `/api/v1/system/info` | GET | chip, IDF version, temperature, uptime, heap, reset reason, LED state |
| `/api/v1/link` | GET | router SSID, BSSID, channel, RSSI, IP, gateway, netmask |
| `/api/v1/led` | POST | `{"r":0-255,"g":0-255,"b":0-255}` or `{"mode":"auto"}` |
| `/api/v1/wifi/status` | GET | setup network, saved network, result of the last connection attempt |
| `/api/v1/wifi/scan` | GET | nearby networks (takes a few seconds) |
| `/api/v1/wifi/provision` | POST | `{"ssid":"...","password":"..."}`: try the network, save it only if it works (answers `202`, poll `wifi/status`) |
| `/api/v1/wifi/forget` | POST | erase the saved network and restart into setup mode |
| `/api/v1/heaptop` | GET | heaptop text, `text/plain`. Query: `view=top\|heap\|tasks\|health`, `sort=cpu\|heap\|stack\|name`, `refresh=50..10000`, `paused=0\|1` (`refresh` only changes the top header; `paused=1` draws the previous sample again) |
| `/api/v1/heaptop/clear` | POST | start a fresh measurement window (`ht clear`) |
| `/api/v1/router` | GET | router lab: lab network, clients, counters, flows, DNS names, lab service requests (`{"enabled":false}` without router lab mode). This is also the export. |
| `/api/v1/router/capture` | POST | `{"enabled":true\|false}`: start or stop the capture (`403` from the lab network) |
| `/api/v1/router/clear` | POST | forget flows, DNS names, lab service requests and counters (`403` from the lab network) |
| `/api/v1/router/lab` | POST | any text: the lab service keeps the start of the body (`409` while capture is off) |

`wifi/provision` only works while the setup network is open (`409` otherwise). The `router/*` POST routes
only exist with router lab mode.

```powershell
curl http://netlab.local/api/v1/system/info
curl -X POST http://netlab.local/api/v1/led -d '{"r":0,"g":0,"b":255}'
curl -X POST http://netlab.local/api/v1/led -d '{"mode":"auto"}'
curl http://netlab.local/api/v1/wifi/status
curl "http://netlab.local/api/v1/heaptop?view=tasks&sort=heap"
```

## Troubleshooting

| Symptom | What to check |
|---|---|
| LED purple, no `Netlab-Fraga` network on the phone | The serial log prints `Setup network '...' is open` when it is up. Move closer to the board: the setup network is 2.4 GHz only. |
| Setup page does not open by itself | Open **http://192.168.4.1/** manually. Turn mobile data off if the phone keeps using it. |
| Setup says "Network not found" | Wrong name, out of range, or a 5 GHz-only network. The S3 needs 2.4 GHz. |
| Setup says "The router did not accept the password" | Retype it (use *Show password*). Nothing was saved. |
| `netlab.local` does not resolve | Use the IP from the serial log (`Got IP ...`). Some networks or systems block mDNS. |
| LED stays red | The saved router cannot be reached. After 90 s the setup network opens so you can pick another. |
| CMake error "front/web/dist doesn't exist" | Run `build.ps1` / `build.sh` (or `pnpm install && pnpm build` in `front/web`), or disable *Flash the web UI* in `menuconfig`. |
| Log says `Cannot mount 'www' partition` | The web UI was not flashed, so only the API and the setup page are served. Rebuild with *Flash the web UI* enabled and flash again. |
| Page shows *Device unreachable* | The S3 is offline or you are on a different network. Check the LED and the serial log. |
| Heaptop page says `heaptop is not running` | The boot log has `Heap monitor disabled (...)`: usually not enough RAM for heaptop's buffers. |
| Heaptop HEAP/PEAK/PSRAM columns show `-` | The build is not using `sdkconfig.defaults`' heap options: delete `sdkconfig` and build again, or enable *Heap task tracking* in `menuconfig`. |

## Notes

- **No authentication on the dashboard/API.** Keep it on your home LAN and never port-forward it. Anyone
  on the LAN can call `wifi/forget` (which restarts the device into setup mode), and anyone who knows
  the setup network password can reconfigure the device while the setup network is open. In router lab
  mode the same goes for lab clients: they can open the dashboard (including the Router lab page with
  every client's records) and call `wifi/forget`; only the capture controls refuse them.
- **Heap monitor costs.** heaptop takes about 32 KB of internal RAM (no PSRAM is enabled): its buffers
  plus its 4 KB sampler task. The boot line `HEAPTOP: started: ...` prints the measured number. The page's
  buffers take about 8 KB more. The options heaptop reads are on in `sdkconfig.defaults`. Heap task
  tracking (`CONFIG_HEAP_TASK_TRACKING`) makes every `malloc`/`free` several times slower, which can skew
  throughput measurements. It also keeps about 25 bytes of bookkeeping per live allocation (the *used*
  BLOCKS in the Heap tab) in internal RAM, and no task's HEAP column includes it. Turn it off in
  `menuconfig` for speed tests; the HEAP, PEAK and PSRAM columns then show `-`. These options only reach
  an existing build after deleting `sdkconfig`.
- `CONFIG_HEAP_TRACK_DELETED_TASKS` stays off, so the Tasks table has no `X` rows for deleted tasks that
  still hold heap. With it on, IDF keeps a record of every task ever deleted. The captive-portal DNS task
  is created and deleted each time the setup network opens, so those records would pile up until they
  push live tasks out of heaptop's table.
- heaptop is pinned to exactly 0.3.0 (`components/heap_monitor/idf_component.yml`) because the page uses
  its internal text renderers. Check `heap_monitor.c` still builds before raising the version.
- With heap task tracking a task must never delete itself (an ESP-IDF 6.0.2 assert, see heaptop's
  *Caveats*): tasks suspend themselves and whoever stops them deletes them, as `dns_catch_all.c` does.
- The Wi-Fi station keeps retrying forever, and modem power save is disabled so latency
  measurements are not skewed.

## Roadmap

1. Skeleton, LED, chip temperature, Wi-Fi link, self-configuring Wi-Fi (this milestone)
2. Network monitor: ping targets, history, outage log, baseline speed test
3. Wi-Fi analyzer: scan, channel congestion chart
4. Bridge mode: softAP + NAT so the S3 can replace the router's Wi-Fi, with a safe fallback (the
   classroom [router lab mode](#router-lab-mode-classroom) is the first step)
5. A/B comparison: direct vs bridged speed, default vs tuned Wi-Fi settings
