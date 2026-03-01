# dbshell — Apache Guacamole Database Shell Protocol Plugin

## Overview

`libguac-client-dbshell` is a **guacd protocol plugin** that adds a native
"database shell" connection type to Apache Guacamole 1.6.0.  It lets users
open an interactive SQL (or MongoDB) shell directly in the browser — through
the same Guacamole web interface used for SSH, VNC, and RDP — with no
additional client software required.

### How it works

```
Browser (Guacamole web app)
       │  WebSocket / HTTP tunnel
       ▼
  guacamole-client (Tomcat)
       │  Guacamole protocol (TCP)
       ▼
     guacd
       │  dlopen("libguac-client-dbshell.so")
       ▼
  dbshell plugin
       │  forkpty() + execvp(mysql / psql / sqlcmd / mongosh / sqlplus)
       ▼
  DB CLI subprocess  ──TCP──►  Database Server
```

The plugin uses **`forkpty()`** to create a pseudoterminal (PTY) pair and
**`execvp()`** to launch the appropriate database CLI binary as a child
process.  The CLI binary connects over the network to the target database
server using its own library/driver — so guacd does **not** implement any
database wire protocol.  The plugin simply pipes the terminal I/O between
the browser and the CLI subprocess.

### Supported databases

| db-type value | CLI binary | Default port |
|---|---|---|
| `mysql` (default) | `mysql` | 3306 |
| `postgresql` | `psql` | 5432 |
| `sqlserver` | `sqlcmd` | 1433 |
| `mongodb` | `mongosh` | 27017 |
| `oracle` | `sqlplus` | 1521 |

---

## File structure

All new files live under:

```
guacamole-server-1.6.0/src/protocols/dbshell/
```

| File | Purpose |
|---|---|
| `settings.h / settings.c` | Parse and store all connection parameters received during the Guacamole protocol handshake |
| `client.h / client.c` | `guac_client_init()` entry point (called by guacd via dlopen); per-connection state struct; free handler |
| `dbshell.h / dbshell.c` | Main I/O thread: creates the terminal, builds the CLI argv, forks via `forkpty()`, relays PTY↔terminal I/O |
| `input.h / input.c` | Keyboard, mouse, and terminal-resize event handlers |
| `clipboard.h / clipboard.c` | Clipboard stream handlers (browser → terminal paste) |
| `user.h / user.c` | User join/leave lifecycle handlers; registers input handlers |
| `Makefile.am` | Autotools build file that produces `libguac-client-dbshell.so` |

---

## Connection parameters

These are the parameters configured in Guacamole (via `user-mapping.xml`,
the JDBC database, or the Guacamole REST API) for a `dbshell` connection.

| Parameter | Required | Default | Description |
|---|---|---|---|
| `db-type` | No | `mysql` | Database type: `mysql`, `postgresql`, `sqlserver`, `mongodb`, `oracle` |
| `hostname` | No | `localhost` | Database server hostname or IP |
| `port` | No | type-dependent | TCP port; defaults to the standard port for `db-type` |
| `username` | No | *(empty)* | Login username |
| `password` | No | *(empty)* | Login password |
| `database` | No | *(empty)* | Database / schema name to connect to |
| `read-only` | No | `false` | Drop all user keyboard input |
| `font-name` | No | `monospace` | Terminal font family |
| `font-size` | No | `12` | Terminal font size (points) |
| `color-scheme` | No | *(default)* | Guacamole color scheme string |
| `scrollback` | No | `1000` | Maximum scrollback buffer rows |
| `disable-copy` | No | `false` | Block clipboard reads from terminal |
| `disable-paste` | No | `false` | Block clipboard paste into terminal |
| `typescript-path` | No | *(disabled)* | Directory for raw session typescript |
| `typescript-name` | No | `typescript` | Typescript filename base |
| `create-typescript-path` | No | `false` | Create typescript directory if missing |
| `recording-path` | No | *(disabled)* | Directory for session screen recording |
| `recording-name` | No | `recording` | Recording filename base |
| `create-recording-path` | No | `false` | Create recording directory if missing |
| `recording-exclude-output` | No | `false` | Omit graphical output from recording |
| `recording-exclude-mouse` | No | `false` | Omit mouse events from recording |
| `recording-include-keys` | No | `false` | Include key events in recording |

---

## Security notes

### Password handling

- **MySQL** — The password is passed as `-p<password>` inline in the CLI argv.
  This is visible in `/proc/<pid>/cmdline` briefly during process startup.
  For better security, consider using a `~/.my.cnf` or MySQL Vault integration
  instead of passing the password in the connection parameters.

- **PostgreSQL** — The password is exported as the `PGPASSWORD` environment
  variable in the child process.  It does **not** appear in the process argv.
  This is the safest approach available without a `.pgpass` file.

- **SQL Server** — `sqlcmd` receives `-P <password>` in the argv.  Same caveat
  as MySQL.  A Kerberos/integrated auth setup avoids this.

- **MongoDB** — `mongosh` receives `-p <password>` in the argv.

- **Oracle** — The password is embedded in the connection string
  `user/password@host:port/db` passed to `sqlplus`.  This is a limitation of
  the `sqlplus` CLI interface.

### Vault integration

The existing `guacamole-vault` extension injects credentials into connection
parameters before they reach guacd.  Because `dbshell` uses the standard
Guacamole parameter naming convention (`username`, `password`), the vault
extension works with `dbshell` connections **without any modification**.

In `guacamole-vault` YAML you can map, for example:

```yaml
# Keeper Secrets Manager mapping for a dbshell connection
- connection: "prod-mysql"
  username: "${ksm://record/ProdMySQL/field/username}"
  password: "${ksm://record/ProdMySQL/field/password}"
```

### Command ACL

The `command-acl.conf` mechanism (used by the SSH plugin to whitelist/blacklist
shell commands) does **not** apply to `dbshell` because the plugin does not
parse individual commands — all input is streamed raw to the CLI subprocess's
stdin.  Restrict access at the database level (grants, roles) and at the
Guacamole connection level (user/group permissions).

---

## Build instructions

### Prerequisites on the guacd host

Install the database CLI clients for all databases you want to support:

```bash
# Debian / Ubuntu
apt-get install mysql-client postgresql-client \
    mssql-tools mongosh

# RHEL / CentOS / Rocky
yum install mysql mariadb postgresql sqlcmd mongosh

# Oracle sqlplus is distributed separately by Oracle
```

`forkpty()` is provided by **glibc** on Linux (no extra package needed).
The `Makefile.am` links `-lutil` for compatibility with older distributions
where `forkpty` lives in `libutil`.

### Build the plugin

The plugin uses the same Autotools infrastructure as the rest of
`guacamole-server`.  After placing the `dbshell/` directory in
`src/protocols/`, you need to register it in the top-level build system:

#### 1 — Register the subdirectory in `configure.ac`

Open `guacamole-server-1.6.0/configure.ac` and add `dbshell` to the
`AC_CONFIG_FILES` list alongside the other protocols:

```
src/protocols/dbshell/Makefile
```

Also add it to the `PROTOCOL_DIRS` variable (if one exists) or to the
`SUBDIRS` list in `src/protocols/Makefile.am`:

```makefile
# src/protocols/Makefile.am  — add dbshell to the list
SUBDIRS = rdp ssh telnet vnc kubernetes dbshell
```

#### 2 — Configure and build

```bash
cd /opt/guacamole-server-1.6.0

# Regenerate Autotools files (only needed once after editing configure.ac)
autoreconf -fi

# Configure (adjust flags to match your existing build)
./configure --with-guacd

# Build only the new plugin
make -C src/protocols/dbshell

# Install
sudo make -C src/protocols/dbshell install
sudo ldconfig
```

The installed library will be at:

```
/usr/local/lib/libguac-client-dbshell.so
```

#### 3 — Verify guacd can load it

```bash
# Check the shared library is visible to the dynamic linker
ldconfig -p | grep libguac-client-dbshell

# Quick smoke-test: attempt to dlopen the library
python3 -c "import ctypes; ctypes.CDLL('libguac-client-dbshell.so')"
```

---

## Configuring a connection in Guacamole

### Option A — user-mapping.xml (no-auth / simple setups)

```xml
<connection name="Production MySQL" protocol="dbshell">
  <param name="db-type">mysql</param>
  <param name="hostname">db.example.com</param>
  <param name="port">3306</param>
  <param name="username">readonly_user</param>
  <param name="password">s3cr3t</param>
  <param name="database">myapp</param>
  <param name="font-name">monospace</param>
  <param name="font-size">13</param>
  <param name="color-scheme">green-black</param>
</connection>

<connection name="Analytics PostgreSQL" protocol="dbshell">
  <param name="db-type">postgresql</param>
  <param name="hostname">pg.example.com</param>
  <param name="username">analyst</param>
  <param name="password">hunter2</param>
  <param name="database">analytics</param>
</connection>
```

### Option B — JDBC database backend

When using `guacamole-auth-jdbc`, create the connection via the Guacamole
admin UI or REST API, selecting protocol **"dbshell"** and filling in the
parameter form.  (A Java-side extension providing the form definition can be
added as a future enhancement — see "Optional: Java client extension" below.)

### Option C — Vault-managed credentials

With `guacamole-vault` (KSM or HashiCorp Vault), set the `username` and
`password` parameters to vault token expressions.  The vault extension
resolves the tokens before guacd sees them, so the plugin receives the
plaintext values without any special handling.

---

## Optional: Java client extension (Guacamole web UI)

For a proper admin UI form (connection type dropdown, parameter labels,
validation), you can create a Guacamole Java extension:

1. Add a new Maven module under
   `guacamole-client-1.6.0/extensions/guacamole-auth-dbshell/`.
2. Register the `dbshell` protocol and its parameters in a
   `guac-manifest.json` and a `translations/en.json` file.
3. Build with Maven and deploy the `.jar` to `/etc/guacamole/extensions/`.

This step is **optional** — the plugin works without it; connections must
just be configured programmatically (XML, JDBC rows, REST API).

---

## Architecture deep-dive

### Plugin loading (guacd side)

When a browser requests a `dbshell` connection, guacd calls:

```c
// src/libguac/client.c
guac_client_load_plugin(client, "dbshell");
// → dlopen("libguac-client-dbshell.so", RTLD_LAZY)
// → dlsym(handle, "guac_client_init")
// → guac_client_init(client)           ← our entry point
```

### Thread model

```
guacd worker process
├── main thread      → guac_client_init() + lifecycle
├── client_thread    → forkpty() + PTY output → guac_terminal
└── input_thread     → guac_terminal_read_stdin() → PTY input
```

The **client_thread** (`guac_dbshell_client_thread`) is the long-running
loop that:
1. Creates the `guac_terminal` (renders the CLI output to the browser canvas).
2. Calls `forkpty()` which atomically creates a PTY master/slave pair and
   forks a child process.
3. The **child** `exec`s the CLI binary (e.g. `mysql`).  The CLI binary
   believes it is connected to a real terminal because its stdin/stdout/stderr
   are all the PTY slave fd.
4. The **parent** polls the PTY master fd.  When data arrives (CLI output),
   it calls `guac_terminal_write()` which renders it in the browser.

The **input_thread** runs concurrently:
1. Calls `guac_terminal_read_stdin()` which blocks until the browser user
   presses a key.
2. Writes the keystroke bytes to the PTY master fd, which the CLI binary
   reads from its stdin.

### Terminal resize

When the browser window resizes, Guacamole sends a `size` instruction.
`guac_dbshell_user_size_handler` calls:
1. `guac_terminal_resize()` — updates the rendering layer.
2. `ioctl(pty_fd, TIOCSWINSZ, &ws)` — notifies the CLI subprocess of the
   new dimensions via the standard PTY mechanism.  The CLI then re-wraps its
   output accordingly.

### Session recording

Calling `guac_recording_create()` attaches a recording sink to the
`guac_client`'s socket.  Every Guacamole protocol message subsequently sent
by `guac_terminal_write()` (and by the input/mouse handlers) is automatically
captured — no explicit "record this byte" calls are required in the plugin.

---

## Limitations and future work

| Limitation | Mitigation / Future work |
|---|---|
| CLI binaries must be installed on the guacd host | Package them in the same container/VM as guacd |
| Passwords may appear briefly in `/proc/<pid>/cmdline` for MySQL/SQL Server/Oracle | Use vault-provided env vars, `.my.cnf`, `.pgpass`, Kerberos, etc. |
| No `command-acl` filtering | Enforce access control at the database server via grants/roles |
| No Java UI extension (admin form) | Implement a `guacamole-auth-dbshell` extension module |
| Single active CLI subprocess per connection | Acceptable for interactive shell use |
| No SSH tunnel / bastion support | Chain with an SSH jump host using the existing SSH protocol |

---

## Quick reference: changed / created files

```
guacamole-server-1.6.0/src/protocols/dbshell/
├── client.c          NEW  guac_client_init() entry point
├── client.h          NEW  guac_dbshell_client struct
├── clipboard.c       NEW  Clipboard stream handlers
├── clipboard.h       NEW  Clipboard handler declarations
├── dbshell.c         NEW  Main I/O thread + forkpty logic
├── dbshell.h         NEW  Thread entry point declaration
├── input.c           NEW  Key / mouse / resize handlers
├── input.h           NEW  Handler declarations
├── Makefile.am       NEW  Autotools build rule
├── settings.c        NEW  Argument parsing
├── settings.h        NEW  Settings struct + arg list
├── user.c            NEW  Join / leave lifecycle
└── user.h            NEW  Lifecycle declarations

guacamole-server-1.6.0/src/protocols/Makefile.am
                      EDIT add "dbshell" to SUBDIRS

guacamole-server-1.6.0/configure.ac
                      EDIT add src/protocols/dbshell/Makefile to AC_CONFIG_FILES
```

No existing files were modified by the plugin code itself.  The two edits to
`Makefile.am` and `configure.ac` are required to wire the new subdirectory
into the Autotools build system.
