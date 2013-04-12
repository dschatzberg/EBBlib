import pexpect
for i in range(1, 65):
    child = pexpect.spawn ('apps/bindtst/bindtst -ebbos_cores ' + str(i))
    for j in range(i):
        child.expect('bindtst: PASSED', timeout=10)
