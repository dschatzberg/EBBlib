import pexpect
for i in range (4):
    child = pexpect.spawn ('qemu-system-x86_64 -nographic -smp ' + str(i+1) + ' apps/bindtst/bindtst.iso')
    for j in range(i+1):
        child.expect('bindtst: PASSED', timeout=10)
