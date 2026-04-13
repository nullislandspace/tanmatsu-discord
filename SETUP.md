# Tanmatsu Discord — Setup Guide

A step-by-step walkthrough from "I have a Tanmatsu" to "messages are
flowing in and out of a Discord channel". You'll create a bot on
Discord's developer portal, invite it to a server, write a small JSON
config file to your SD card, and flash the app.

> **Bot, not user account.** Discord forbids automating personal user
> accounts. This app uses an official **bot account** with a **bot
> token** — that's the only legal automation path. You'll only see and
> post in channels that an admin has explicitly invited the bot to.

---

## Part A — Create the Discord bot

1. Sign in at <https://discord.com/developers/applications>.
2. Click **New Application** → name it (e.g. `Tanmatsu Bridge`) →
   **Create**.
3. In the left sidebar, open **Bot**.
   - Optionally set a username and avatar.
   - Under **Privileged Gateway Intents**, toggle
     **MESSAGE CONTENT INTENT** to **ON**.
     Without this, you'll receive message events but every `content`
     field will be empty.
   - Click **Reset Token**, then **Copy**.
     **Discord shows the token only once.** Save it somewhere safe —
     if you lose it, your only option is Reset (which invalidates the
     old one).
4. In the left sidebar, open **OAuth2 → URL Generator**.
   - **Scopes**: tick **bot**.
   - **Bot Permissions**: tick at minimum
     - **View Channels**
     - **Send Messages**
     - **Read Message History**
     - **Attach Files**
   - Copy the generated URL at the bottom of the page.

## Part B — Invite the bot to a server

The OAuth2 URL from Part A is the **invite link** for your bot. Whoever
clicks it picks a target server from a dropdown, confirms the requested
permissions, and the bot joins. They must have **Manage Server** on
that target. The token is **not** part of the invite URL — you never
need to share your token to add the bot to a server.

A bot can live in many servers at once. Same token, same application,
just invited separately to each one.

### B1 — Inviting to a server you administer

1. Paste the OAuth2 URL into a browser.
2. Pick your server in the dropdown.
3. Confirm permissions → **Authorize**.
4. If the target channel is **private**, also go to that channel's
   **settings → Permissions → Members & Roles** and add the bot's
   role explicitly with the same four permissions.

### B2 — Inviting to a friend's server (you don't admin)

You can't add a bot to a server you don't administer — Discord blocks
this. You have to ask the server admin to do the invite for you. Send
them:

1. **The OAuth2 URL** from Part A. Just the URL — keep your token
   private, they don't need it.
2. A short note, e.g.:

   > Could you add this bot to your Discord server? Click the link,
   > pick the server, and Authorize. It just needs View Channels,
   > Send Messages, Read Message History, and Attach Files.

After they authorize, ask them to send you back:

- The **server ID** (right-click the server name with Developer Mode
  on → **Copy Server ID**).
- The **channel ID(s)** of every channel the bot should be able to
  read/post in (right-click channel → **Copy Channel ID**).
- For any **private** channel: they also need to add the bot's role
  to that channel's permissions, otherwise the bot won't see it.

You then add those IDs to `discord.json` (Part D) and reboot — the new
channel(s) appear in the device's channel list automatically.

## Part C — Get the IDs you need

The app references servers and channels by their numeric **snowflake
IDs**, not their names.

1. In the Discord desktop or web client, go to
   **User Settings → Advanced → Developer Mode** and turn it **ON**.
2. Right-click the **server name** (top of the channel list) →
   **Copy Server ID**. That's your `guild_id`.
3. Right-click each **channel** the bot should access →
   **Copy Channel ID**. Those are your `channel_id` values.

IDs are 18–19 digit numbers. **Always quote them as strings in JSON.**
Bare JSON numbers lose precision past 2⁵³.

## Part D — Write `discord.json` on the SD card

1. On a computer, create a file called `discord.json` with this
   content (replace the placeholders with your real values):

   ```json
   {
     "token": "YOUR-TOKEN-FROM-PART-A",
     "default_channel": "YOUR_PREFERRED_CHANNEL_ID",
     "channels": [
       { "guild_id": "YOUR_SERVER_ID",
         "channel_id": "YOUR_CHANNEL_ID",
         "name": "general" },
       { "guild_id": "YOUR_SERVER_ID",
         "channel_id": "ANOTHER_CHANNEL_ID",
         "name": "off-topic" }
     ]
   }
   ```

   Notes on each field:

   - `token` — the raw token from Part A. The literal `Bot ` prefix
     used in Discord's HTTP `Authorization` header is **optional** here:
     this app accepts either `"<token>"` or `"Bot <token>"` and
     normalizes internally.
   - `default_channel` — optional; the `channel_id` to highlight first
     when the channel list opens. Must also appear in `channels`. Omit
     if you don't care.
   - `channels[]` — every channel the bot should read/post in. Each
     entry needs `guild_id` + `channel_id`; `name` is the label shown
     in the device's channel list.

2. Save it to the **root** of the SD card, named exactly
   `discord.json`. On the device this becomes `/sd/discord.json`.

3. **Treat this file like a password.** Anyone with the token can
   post as your bot. If it leaks (committed to git, posted in chat,
   etc.) go back to Part A → Bot → **Reset Token** to invalidate the
   old one and update the file.

> **Adding a new channel later**: just edit `discord.json`, add a new
> entry to `channels`, save. The next time the app boots and connects,
> the new channel will show up in the list and get an initial backlog
> fetch (up to 100 messages) automatically. No code change required.

## Part E — Use it on the Tanmatsu

1. **First**, configure WiFi through the **launcher's** WiFi settings
   page. This app reuses whatever the launcher knows — there's no
   separate WiFi UI here.
2. Insert the SD card with `discord.json`, install the app, and start
   it.
3. Boot sequence (each line is a screen you may see briefly):
   - "Mounting SD card…"
   - "Connecting to radio…" → "Starting WiFi stack…" →
     "Connecting to WiFi network…" → "Connected to WiFi network"
   - "Loading /sd/discord.json…"
   - "Logging in to Discord…"
   - **Channel list** populated from your config.

### Keys

**Channel list**

| Key       | Action                          |
|-----------|---------------------------------|
| Up / Down | Move selection                  |
| Enter     | Open the highlighted channel    |
| **F1**    | Exit to launcher                |
| **F2 / F3** | Backlight dim / bright       |

**Chat view**

| Key                    | Action                                |
|------------------------|---------------------------------------|
| Any printable key      | Start composing (text appears at bottom) |
| Backspace              | Delete one character                  |
| Enter (while composing)| Send                                  |
| Esc (while composing)  | Cancel compose                        |
| Esc (otherwise)        | Back to channel list                  |
| Up / Down              | Scroll history                        |
| **F1**                 | Exit to launcher                      |
| **F2 / F3**            | Backlight dim / bright                |

## Local message storage

While running, the app saves every message it sees (live + initial
backlog) to a per-channel JSONL file under `/sd/discord/`:

```
/sd/discord/
├── 406116749382254603.jsonl
├── 406117047295148032.jsonl
└── …
```

Each file is capped at the most recent **100 messages** (older entries
are trimmed automatically). When you open a channel, the chat view is
populated from this file immediately — even before the network is
involved — so you can scroll recent history offline.

## Part F — Troubleshooting

| Symptom on screen / log                                       | Cause / fix |
|---------------------------------------------------------------|-------------|
| `SD card missing or unreadable.`                              | No SD card inserted, or unsupported filesystem. Use a FAT-formatted card. |
| `Config missing or invalid.`                                  | File isn't at SD root, isn't named exactly `discord.json`, or contains invalid JSON. |
| `Failed to connect to WiFi network`                           | Configure WiFi networks in the **launcher** first; this app reuses them. |
| Login appears OK but `dcapi_request: 401: Unauthorized` on send | Token is wrong. Re-copy from the dev portal (Reset if needed). The `Bot ` prefix is optional but the rest must be exact, no leading/trailing spaces. |
| Login fails with `DISALLOWED_INTENTS`                         | **MESSAGE CONTENT INTENT** is off in the dev portal — Part A step 3. |
| Channel list shows up but no messages arrive                  | (a) No one is posting; (b) MESSAGE CONTENT INTENT off; (c) bot isn't actually in that channel — check Part B. |
| Send fails with HTTP 403                                      | Bot's role lacks **Send Messages** in that channel — Part B step 4. |
| Hint bar shows `F1`/`Esc` as plain text rather than icons     | The launcher's `/int/icons/*.png` aren't reachable. Install/run via the launcher to get them mounted; the text fallback is harmless. |
