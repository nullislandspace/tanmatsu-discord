# esp-discord on Tanmatsu — Investigation

Source: https://github.com/abobija/esp-discord (v2.0.1, Oct 2024, MIT)

## Verdict

Usable as-is. esp-discord is a **pure ESP-IDF component** (library), not
an application. It has no `app_main`, no UI, and nothing chip-specific
to adapt. You add it as a managed dependency and call its API from your
Tanmatsu app once Wi-Fi is up.

## Compatibility

- **ESP-IDF**: requires `^5.3`; Tanmatsu uses `>=5.5.1` ✓
- **Target**: pure C, no peripheral dependencies — builds for ESP32-P4.
- **Network**: needs TCP/IP + DNS up before `discord_login()`. Use
  `tanmatsu-wifi` (and `esp-hosted-tanmatsu` on P4) to bring Wi-Fi up
  first, then start the bot — equivalent to `example_connect()` in the
  upstream echo sample.
- **License**: MIT, compatible.

## Dependencies pulled in

- `espressif/esp_websocket_client ^1.2.3`
- `json` (cJSON), `esp_http_client`, `app_update`, `nvs_flash`
- Embeds two TLS certs (`gateway.pem`, `api.pem`) auto-generated at
  build time by `certgen.sh` — needs `openssl` on the build host. Or
  set `CONFIG_ESP_TLS_SKIP_SERVER_CERT_VERIFY` to skip cert pinning.

## Integration steps

1. Add to `main/idf_component.yml`:
   ```yaml
   dependencies:
     abobija/esp-discord: "^2.0.1"
   ```
2. No CMake change needed beyond that — the managed component registers
   itself.
3. After `tanmatsu-wifi` reports connected:
   ```c
   discord_config_t cfg = {
       .token   = "Bot <YOUR_TOKEN>",
       .intents = DISCORD_INTENT_GUILD_MESSAGES
                | DISCORD_INTENT_MESSAGE_CONTENT,
   };
   discord_handle_t bot = discord_create(&cfg);
   discord_register_events(bot, DISCORD_EVENT_ANY, bot_event_handler, NULL);
   discord_login(bot);
   ```
   See `examples/echo/main/echo.c` upstream for the full pattern.

## Features available

- **Reading**: gateway events `DISCORD_EVENT_MESSAGE_RECEIVED /
  UPDATED / DELETED`, plus reaction/member/guild events via intents
  (`GUILD_MESSAGES`, `MESSAGE_CONTENT`, `DIRECT_MESSAGES`, reactions,
  presence, etc.).
- **Posting**: `discord_message_send()`, embeds, reactions.
- Models for guild / channel / member / role / emoji / attachment /
  voice_state / embed.
- OTA helper (`discord_ota.h`).

## Limitations

- **Bot tokens only** — no user OAuth2. A Discord application + bot
  must be created and invited to each guild ("subgroup") you want to
  read/post in.
- **No voice, no sharding, no slash-command registration helpers**
  (slash commands would have to be registered via REST manually).
- Buffers default to several KB each; fine on P4 with PSRAM but tune
  `gateway_buffer_size` / `api_buffer_size` in `discord_config_t` if
  needed.
- Low release cadence (last release Oct 2024) but the API is stable.

## Wi-Fi in the template app

The template (`main/main.c:150-181`) already brings up Wi-Fi:

1. `wifi_remote_initialize()` — powers on the radio coprocessor.
2. `wifi_connection_init_stack()` — starts the ESP-IDF Wi-Fi stack.
3. `wifi_connect_try_all()` — tries all stored networks.

`wifi_connect_try_all()` is the same call the Tanmatsu launcher uses
(`tanmatsu-launcher/main/main.c:149`), via the shared `tanmatsu-wifi`
component. It reads SSIDs/passwords from NVS, and the launcher's
settings UI writes networks into that same NVS namespace.

**Result**: any Wi-Fi network configured through the launcher is
automatically picked up by this app — no separate provisioning UI or
config needed. Just gate `discord_login()` on `wifi_connect_try_all()`
returning `ESP_OK`.

## Recommended app architecture

- Gate `discord_login()` on a Wi-Fi-up event from `tanmatsu-wifi`.
- In the event handler, push incoming messages into a FreeRTOS queue
  consumed by the PAX UI task — do not render from the gateway
  callback, or a slow redraw will stall the WebSocket.
- Store the bot token in NVS, not in source.
- Keep one `discord_handle_t` for the lifetime of the app; reconnects
  are handled internally.

## Authentication & access

Discord supports only one auth method for automation: a **bot token**.
No username/password, no user-account login.

1. Create an Application at https://discord.com/developers/applications
2. In the Bot tab, "Reset Token" — copy once.
3. Enable the **Message Content Intent** toggle (required for
   `DISCORD_INTENT_MESSAGE_CONTENT`).
4. Pass to esp-discord as `cfg.token = "Bot <TOKEN>"`.

A bot **cannot accept invite links itself** — a server admin (someone
with `Manage Server`) must add it via an OAuth2 invite URL generated
in the dev portal (scope `bot`, plus the desired permissions like
Send Messages, Read Message History, Attach Files).

**Consequence**: you cannot put the bot into a server you don't
own/admin unless an admin of that server invites it for you. This is a
Discord-side restriction, not a library limitation.

## Identifying servers and channels

Discord uses **snowflake IDs** (18–19 digit numeric strings), not
names. Enable Developer Mode in the Discord client (Settings →
Advanced), then right-click a server/channel → "Copy ID".

Store IDs as **strings** — snowflakes are 64-bit and JSON numbers in
cJSON lose precision past 2^53. The esp-discord API takes them as
`char*` anyway.

## On-device configuration

Bot tokens are ~70 chars of opaque base64; channel/guild IDs are 18–19
digit numbers. Not realistic to type on a handheld keyboard — one
wrong character = silent auth failure.

Recommended approach: **JSON config file on the SD card**. cJSON is
already pulled in by esp-discord, FATFS is already in the template's
`PRIV_REQUIRES`.

Suggested layout (`/sd/discord.json`):

```json
{
  "token": "MTEwNzc4...GhIjKl.mNoPqR-sTuVwXyZ",
  "channels": [
    { "guild_id": "123456789012345678",
      "channel_id": "234567890123456789",
      "name": "general" },
    { "guild_id": "123456789012345678",
      "channel_id": "345678901234567890",
      "name": "tanmatsu-chat" }
  ]
}
```

Boot flow: mount SD → read file → `cJSON_Parse` → extract token +
channel list → optionally cache to NVS so the SD card can be removed.

Treat the file like a password file. Provide a "reset token" path
(regenerate in dev portal, drop new file) for when it leaks.

## Discord Terms of Service

**User account vs. bot account**

- **User account**: tied to a human, email/password + 2FA, used by the
  official clients. **Automating a user account is prohibited** by
  Discord ToS — no scripting your own login, no "self-bots",
  account termination is the consequence.
- **Bot account**: created from the Developer Portal, authenticated
  with a static token, marked with a "BOT" tag, governed by the
  Developer ToS and Developer Policy. Subject to gateway intents and
  bot-specific rate limits.

esp-discord is bot-only — there is no legal path to user-token
automation, so this restriction shapes the app's UX.

**What's allowed**

- Bots that read and post in servers they've been invited to.
- Bots that bridge Discord to other systems (this app's category).
- Human-triggered actions like "press a button on Tanmatsu → bot
  posts a photo to channel X" — textbook bot use case.

**What's NOT allowed**

- Logging into a personal Discord account from the device.
- Reading DMs/servers you're in personally without going through the
  bot invite process.
- Any "Discord client" that impersonates the official client with
  user credentials.

**UX consequence**

The Tanmatsu app is a **bot remote**, not a personal Discord client.
Channels visible = channels the bot was invited into. Outgoing
messages are authored by the bot, not by you. To use it in a server
you participate in personally, that server's admin must invite the
bot.

## Photo sharing (user-triggered)

Posting a Tanmatsu-captured photo to a Discord channel on user request
is squarely within ToS — clearly bot-authored, human-triggered, in a
channel the bot was invited to.

Practical considerations:

- **Permissions**: bot's role needs `Send Messages` + `Attach Files`
  in the target channel.
- **Intents**: posting requires no privileged intent (only *reading*
  message content does).
- **Rate limits**: ~5 msg / 5 sec per channel for bots — fine for
  human-triggered sends. Library handles 429s.
- **File size**: 25 MB for non-Nitro uploads. JPEG-compress before
  sending; raw framebuffers would be too large anyway.
- **Endpoint**: attachments go via `POST /channels/{id}/messages` as
  `multipart/form-data`. **Verify** esp-discord's `discord_message_send()`
  supports multipart attachments — its public API focuses on
  text+embeds. If not supported, fall back to one of:
   - `esp_http_client` directly to the messages endpoint with the
     bot token in the `Authorization` header,
   - upload the JPEG to external storage (e.g. S3 presigned URL) and
     post the link as a plain message.
