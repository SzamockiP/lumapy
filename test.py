import lumapy

def main():
    engine = lumapy.Engine()
    engine.init(1024, 720, "test")

    @engine.onError
    def error(msg):
        print(msg)

    pos = [0,0]

    @engine.onFrame
    def fun():
        nonlocal pos
        if engine.isKeyPressed(ord('A')):
            pos[0] -= 0.05;
            engine.log(str(pos))
        if engine.isKeyPressed(ord('W')):
            pos[1] += 0.05;
            engine.log(str(pos))
        if engine.isKeyPressed(ord('S')):
            pos[1] -= 0.05;
            engine.log(str(pos))
        if engine.isKeyPressed(ord('D')):
            pos[0] += 0.05;
            engine.log(str(pos))

        if engine.isKeyPressed(ord("X")):
            engine.stop()

    engine.run()
    print("End")

if __name__ == "__main__":
    main()