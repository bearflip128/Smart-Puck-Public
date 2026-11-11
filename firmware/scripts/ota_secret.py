# PlatformIO pre-build script: gives the firmware its over-the-air update password and passes the
# same password to espota (via ota_auth.py, which runs after the platform sets its upload flags) for `pio run -e ota -t upload`. The password is generated once into
# ota_password.txt next to platformio.ini (git-ignored), so it never lands in the repo.
import os
import secrets

Import("env")  # noqa: F821  (provided by PlatformIO)

path = os.path.join(env["PROJECT_DIR"], "ota_password.txt")
if not os.path.exists(path):
    with open(path, "w") as f:
        f.write(secrets.token_urlsafe(12))
with open(path) as f:
    password = f.read().strip()

env.Append(CPPDEFINES=[("OTA_PASSWORD", env.StringifyMacro(password))])
