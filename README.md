# wp-notifyd

A daemon that adds notifications to Wireplumber.

Currently, these actions show a notification:

- Changing a default device (a default sink or source)
- Changing volume on a default device
- Muting or Unmuting a default device

## Installing
```bash
git clone https://github.com/LDAP/wp-notifyd
cd wp-notifyd
meson setup build
cd build
meson install
```

## Running
```bash
wp-notifyd
```

## Uninstall
```bash
cd build
sudo ninja uninstall
```
