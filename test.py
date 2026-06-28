import lumapy
import time

eng = lumapy.Engine()

eng.init()

@eng.onError
def error(msg):
    print(msg)

def fun():
    for x in range(100):
        time.sleep(0.1)
        for i in range(10):
            eng.log(str(i))
    eng.stop()

eng.run(fun)
print("koniec")