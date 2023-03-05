# wp-notifyd

A notification daemon for Wireplumber.

Currently, these actions show a notification:

- Changing a default device (a default sink or source)
- Changing volume on a default device
- Muting or Unmuting a default device

## Installation

### Building from source
```bash
git clone https://github.com/LDAP/wp-notifyd
cd wp-notifyd
meson setup build
cd build
meson compile

# Running
./wp-notifyd

# Install
meson install

# Uninstall
sudo ninja uninstall
```

### Debug
Compile with `--buildtype=debug`
```bash
meson setup build --buildtype=debug
cd build
meson compile
```

