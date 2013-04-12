import pexpect
for i in range(64):
    child = pexpect.spawn ('apps/bindtst/bindtst -ebbos_cores ' + str(i+1))
    for j in range(i+1):
        child.expect('bindtst: PASSED', timeout=10)
