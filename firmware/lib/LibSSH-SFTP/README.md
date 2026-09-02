# LibSSH-SFTP

Minimal local SFTP extension for the UniGeek firmware.

Upstream: libssh 0.11.5

Local adaptations:
- config.h -> libssh_esp32_config.h
- sibling libssh header includes -> <libssh/...>
- WITH_SFTP enabled only in local SFTP translation units
- asynchronous SFTP sources omitted
