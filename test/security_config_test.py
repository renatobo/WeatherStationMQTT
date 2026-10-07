"""Exercise credential validation/header generation with synthetic data only."""
import hashlib
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from security_config import device_for, read, header

with tempfile.TemporaryDirectory() as folder:
    root = Path(folder)
    ota = 'Synthetic_OTA_Password_For_Test_Only'
    portal = 'Synthetic_AP_Password_For_Test_Only'
    path = root / 'mysecret_envs.ini'
    path.write_text('[security:workshop]\nota_password = ' + ota +
                    '\nprovisioning_password = ' + portal + '\n')
    _, values = read(root, 'workshop')
    generated = header(values)
    assert ota not in generated
    assert hashlib.md5(ota.encode()).hexdigest() in generated
    assert portal in generated
    assert device_for('workshop_ota') == device_for('workshop') == 'workshop'
    assert device_for('office_ota') == 'office'
    path.write_text('[security:workshop]\nota_password = short\nprovisioning_password = ' + portal)
    try:
        read(root, 'workshop')
        raise AssertionError('Weak password accepted')
    except ValueError:
        pass
    path.write_text('[security:workshop]\nota_password = ' + ota + '\nprovisioning_password = ' + ota)
    try:
        read(root, 'workshop')
        raise AssertionError('Shared OTA/AP password accepted')
    except ValueError:
        pass
print('Synthetic credential validation and private-header checks passed.')
