"""Select the private in-process authenticator after PlatformIO configures OTA."""
from pathlib import Path
from SCons.Script import COMMAND_LINE_TARGETS
Import('env')

if env.GetProjectOption('upload_protocol', '') == 'espota':
    script = Path(env.subst('$PROJECT_DIR')) / 'scripts' / 'ota_upload.py'
    env.Replace(UPLOADCMD='"$PYTHONEXE" "' + str(script) +
                '" --env "$PIOENV" --binary "$SOURCE" --uploader "$UPLOADER"' +
                (' --filesystem' if 'uploadfs' in COMMAND_LINE_TARGETS else ''))
