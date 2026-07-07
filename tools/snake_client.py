import argparse
import json
import os
import socket
import sys
import threading
import time


PORT = 4242


class Keyboard:
    def __enter__(self):
        if os.name == "nt":
            import msvcrt

            self.msvcrt = msvcrt
            return self

        import termios
        import tty

        self.termios = termios
        self.fd = sys.stdin.fileno()
        self.old = termios.tcgetattr(self.fd)
        tty.setcbreak(self.fd)
        return self

    def __exit__(self, exc_type, exc, tb):
        if os.name != "nt":
            self.termios.tcsetattr(self.fd, self.termios.TCSADRAIN, self.old)

    def get_key(self):
        if os.name == "nt":
            if not self.msvcrt.kbhit():
                return None

            ch = self.msvcrt.getch()
            if ch in (b"\x00", b"\xe0"):
                code = self.msvcrt.getch()
                return {
                    b"H": "U",
                    b"P": "D",
                    b"K": "L",
                    b"M": "R",
                }.get(code)

            try:
                return ch.decode("utf-8").lower()
            except UnicodeDecodeError:
                return None

        import select

        ready, _, _ = select.select([sys.stdin], [], [], 0)
        if not ready:
            return None
        return sys.stdin.read(1).lower()


def clear_screen():
    if os.name == "nt":
        os.system("cls")
    else:
        print("\033[H\033[J", end="")


def render(state, connected=True):
    clear_screen()
    if not connected:
        print("Connexion perdue avec l'ESP32.")
        return

    if not state:
        print("Connexion OK. En attente du premier etat du jeu...")
        return

    width = state["w"]
    height = state["h"]
    snake = [tuple(part) for part in state["snake"]]
    snake_set = set(snake)
    food = tuple(state["food"])

    lines = [
        f"Snake ESP32 | Score: {state['score']} | r: reset | x: quitter",
        "Controle: fleches ou ZQSD/WASD",
        "+" + "-" * width + "+",
    ]

    for y in range(height):
        row = []
        for x in range(width):
            p = (x, y)
            if snake and p == snake[0]:
                row.append("@")
            elif p in snake_set:
                row.append("o")
            elif p == food:
                row.append("*")
            else:
                row.append(" ")
        lines.append("|" + "".join(row) + "|")

    lines.append("+" + "-" * width + "+")
    if state["over"]:
        lines.append("Game over. Appuie sur r pour recommencer.")

    print("\n".join(lines), flush=True)


def reader(sock, shared):
    file = sock.makefile("r", encoding="utf-8", newline="\n")
    try:
        for line in file:
            line = line.strip()
            if not line:
                continue
            try:
                state = json.loads(line)
            except json.JSONDecodeError:
                continue
            with shared["lock"]:
                shared["state"] = state
                shared["dirty"] = True
    finally:
        with shared["lock"]:
            shared["connected"] = False
            shared["dirty"] = True


def key_to_command(key):
    return {
        "z": "U",
        "w": "U",
        "U": "U",
        "s": "D",
        "D": "D",
        "q": "L",
        "a": "L",
        "L": "L",
        "d": "R",
        "R": "R",
        "r": "N",
    }.get(key)


def main():
    parser = argparse.ArgumentParser(description="Client PC pour Snake sur ESP32")
    parser.add_argument("host", help="Adresse IP de l'ESP32, ex: 192.168.4.1")
    parser.add_argument("--port", type=int, default=PORT)
    args = parser.parse_args()

    shared = {
        "state": None,
        "dirty": True,
        "connected": True,
        "lock": threading.Lock(),
    }

    try:
        sock = socket.create_connection((args.host, args.port), timeout=10)
    except TimeoutError:
        print(f"Impossible de joindre {args.host}:{args.port} (timeout).")
        print("Verifie l'IP affichee dans le moniteur serie et que le PC est sur le meme Wi-Fi.")
        return
    except OSError as exc:
        print(f"Impossible de se connecter a {args.host}:{args.port}: {exc}")
        print("Attends quelques secondes apres le reset ESP32, puis relance avec l'IP actuelle.")
        return

    with sock:
        sock.settimeout(None)
        threading.Thread(target=reader, args=(sock, shared), daemon=True).start()

        with Keyboard() as keyboard:
            try:
                while True:
                    with shared["lock"]:
                        dirty = shared["dirty"]
                        state = shared["state"]
                        connected = shared["connected"]
                        shared["dirty"] = False

                    if dirty:
                        render(state, connected)

                    if not connected:
                        break

                    key = keyboard.get_key()
                    if key == "x":
                        break

                    command = key_to_command(key)
                    if command:
                        sock.sendall((command + "\n").encode("utf-8"))

                    time.sleep(0.03)
            except KeyboardInterrupt:
                pass


if __name__ == "__main__":
    main()
