import argparse
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
                    b"H": "UP",
                    b"P": "DOWN",
                    b"K": "LEFT",
                    b"M": "RIGHT",
                }.get(code)

            try:
                return ch.decode("utf-8").lower()
            except UnicodeDecodeError:
                return None

        import select

        ready, _, _ = select.select([sys.stdin], [], [], 0)
        if not ready:
            return None

        ch = sys.stdin.read(1)
        if ch == "\x1b":
            sequence = sys.stdin.read(2)
            return {
                "[A": "UP",
                "[B": "DOWN",
                "[D": "LEFT",
                "[C": "RIGHT",
            }.get(sequence)
        return ch.lower()


def reader(sock, shared):
    file = sock.makefile("r", encoding="utf-8", newline="\n")
    try:
        for _ in file:
            pass
    finally:
        with shared["lock"]:
            shared["connected"] = False


def key_to_command(key):
    return {
        "1": "SINGLE",
        "2": "MULTI",
        "m": "MENU",
        "r": "RESET",
        "z": "P1U",
        "w": "P1U",
        "UP": "P1U",
        "s": "P1D",
        "DOWN": "P1D",
        "q": "P1L",
        "a": "P1L",
        "LEFT": "P1L",
        "d": "P1R",
        "RIGHT": "P1R",
        "i": "P2U",
        "k": "P2D",
        "j": "P2L",
        "l": "P2R",
    }.get(key)


def main():
    parser = argparse.ArgumentParser(description="Client PC pour Snake sur ESP32")
    parser.add_argument("host", help="Adresse IP de l'ESP32, ex: 192.168.4.1")
    parser.add_argument("--port", type=int, default=PORT)
    args = parser.parse_args()

    shared = {"connected": True, "lock": threading.Lock()}

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
                        connected = shared["connected"]

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
