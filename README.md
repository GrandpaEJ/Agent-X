# Agent X Pro

Agent X Pro is a production-grade, ultra-lightweight autonomous AI agent written in pure C11. Operating with an extremely tiny RAM footprint (< 500 KB) and compiling to a static binary with zero dynamic library dependencies, it is optimized for high-performance daemonization on VPS, Debian/Ubuntu servers, and Termux (Android).

---

## Key Capabilities

1. **Dynamic Skill Engine**: Register and run new tools dynamically by dropping JSON schemas (`skills/<name>.json`) and scripts (`skills/<name>.sh` or `.py`) into the `skills/` directory—no recompilation needed.
2. **Persistent Memory Store (RAG)**: Uses a lightweight file-based keyword database for long-term fact serialization (`memorize` and `recall` tools).
3. **Dynamic Context Truncation**: Strips large tool outputs from conversation logs older than 3 turns to optimize context length and slash input token costs by up to 70%.
4. **POSIX Daemonization**: Fully daemonizes background Telegram bots via double-forking (`--daemon` flag), mapping PID files and structured log files.
5. **Structured JSON Auditing**: Outputs diagnostic JSON logs to `agent-x.log` for easy troubleshooting.
6. **Multi-Session Isolation**: Automatically isolates and persists individual chat sessions by Telegram `chat_id` and CLI.

---

## Configuration (`.env`)

Configure the agent's behavior via a `.env` file in the workspace root:

```ini
# Core Configuration
SAFE_MODE=1                 # 1 = Prompt user for dangerous tool actions, 0 = fully autonomous
SANDBOX_RESTRICT=0          # 1 = Strict sandboxing (only reads/writes in current workspace)

# API Connectivity (Compatible with any OpenAI format)
API_BASE_URL=https://text.pollinations.ai/openai
API_KEY=your-api-key-here   # (Leave empty if none required)
API_MODEL=openai

# Telegram bot settings
TELEGRAM_BOT_TOKEN=your-telegram-token
ALLOWED_TELEGRAM_USER_IDS=12345678,98765432
```

---

## Compilation

Build targets using static compilation with `zig cc` (or `musl-gcc`):

```bash
make clean
make        # Build agent-x (131 KB - CLI + Telegram Daemon)
make nano   # Build agent-x-nano (98 KB - Interactive CLI with history)
make pico   # Build agent-x-pico (80 KB - Embedded CLI)
```

---

## Running

### Interactive CLI Mode
```bash
./agent-x cli
```

### Background Telegram Daemon Mode
To start the Telegram bot in the background:
```bash
./agent-x telegram --daemon
```
* The background process forks, writes its PID to `agent-x.pid`, and outputs logs to `agent-x.log`.
* To stop the daemon:
```bash
kill $(cat agent-x.pid)
```

---

## Extending with Custom Skills

To add a new tool (e.g. `get_weather`):
1. Create a schema definition `skills/get_weather.json`:
   ```json
   {
     "type": "function",
     "function": {
       "name": "get_weather",
       "description": "Get current weather in a city",
       "parameters": {
         "type": "object",
         "properties": {
           "city": {"type": "string", "description": "City name"}
         },
         "required": ["city"]
       }
     }
   }
   ```
2. Create the executable script `skills/get_weather.sh`:
   ```bash
   #!/bin/bash
   curl -s "https://wttr.in/${ARG_city}?format=3"
   ```
3. Make it executable:
   ```bash
   chmod +x skills/get_weather.sh
   ```
Agent X Pro will automatically detect, register, and execute the tool. Arguments are passed as environment variables prefixed with `ARG_` (e.g. `$ARG_city`).

---

## Zero-Java Smali/DEX Pipeline

Agent X Pro includes a high-performance native Dalvik Smali compiler and DEX parser written in C11. This enables repacking and modifying Android APKs with zero Java/JDK runtime dependencies.

### Assemble Smali to DEX
```bash
./agent-x tool smali_assemble src_dir=path/to/smali out_dex=path/to/classes.dex
```

### Disassemble DEX to Smali
```bash
./agent-x tool read_dex path/to/classes.dex
```

