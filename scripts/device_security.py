"""Generate ignored credential header and use an uploader that keeps secrets out of argv."""
import sys
from pathlib import Path

Import('env')
root = Path(env.subst('$PROJECT_DIR'))
sys.path.insert(0, str(root / 'scripts'))
from security_config import device_for, read, header

device = env.GetProjectOption('custom_security_device', '') or device_for(env.subst('$PIOENV'))
_, values = read(root, device)
folder = Path(env.subst('$BUILD_DIR')) / 'private'
folder.mkdir(parents=True, exist_ok=True, mode=0o700)
folder.chmod(0o700)
target = folder / 'device_security.h'
contents = header(values)
if not target.exists() or target.read_text() != contents:
    target.write_text(contents)
target.chmod(0o600)
env.Append(CPPPATH=[str(folder)])
