# hmi.py - HMI del banco de pruebas (PC): Tkinter + Matplotlib + cliente TCP
# Instalar una vez:  pip install matplotlib
import json
import socket
import threading
import queue
from collections import deque
import tkinter as tk
from matplotlib.figure import Figure
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg

IP_POR_DEFECTO = "127.0.0.1"   # simulador. Con el ESP32 real: la IP que sale en Thonny
PUERTO = 2026                  # el mismo PUERTO del main.py del ESP32
N_PUNTOS = 60                  # cuantas muestras se ven en la grafica

# ---------- COLORES DEL DISENO ----------
PAGINA = "#FCEFE3"     # fondo de la ventana (no venia en la maqueta: cambialo si quieres)
TARJETA = "#FFFDF9"
BORDE = "#F4E3D6"
TEXTO = "#4A3A33"
GRIS = "#78625A"
AZUL = "#3F86B8"
NARANJA = "#D2703F"
FUENTE = "Segoe UI"

cola = queue.Queue()   # el hilo de red deja aqui lo que llega; la ventana lo lee
sock = None            # socket abierto (None = sin conexion)
conectado = False      # True = el usuario quiere estar conectado
hilo = None
pausado = False
rpm = deque(maxlen=N_PUNTOS)
duty = deque(maxlen=N_PUNTOS)


# ---------- HILO DE RED (nunca toca la ventana, solo usa la cola) ----------
def hilo_red(ip):
    global sock
    try:
        s = socket.create_connection((ip, PUERTO), timeout=3)
    except OSError as e:
        cola.put(("estado", "No se pudo conectar: %s" % e))
        cola.put(("fin", None))
        return

    s.settimeout(1)              # cada 1 s revisa si pidieron desconectar
    sock = s
    cola.put(("estado", "Conectado a %s:%d" % (ip, PUERTO)))
    buf = b""
    while conectado:
        try:
            datos = s.recv(1024)
        except socket.timeout:
            continue
        except OSError:
            break
        if not datos:
            break                # el ESP32 cerro la conexion
        buf += datos
        while b"\n" in buf:
            linea, buf = buf.split(b"\n", 1)
            try:
                dato = json.loads(linea.decode())
            except ValueError:
                continue         # linea rota: se ignora
            if isinstance(dato, dict):
                cola.put(("dato", dato))

    s.close()
    sock = None
    cola.put(("estado", "Desconectado"))
    cola.put(("fin", None))


# ---------- BOTONES ----------
def conectar_desconectar():
    global conectado, hilo
    if conectado:
        conectado = False        # el hilo se entera y termina solo
        return
    if hilo is not None and hilo.is_alive():
        return                   # el hilo anterior todavia esta cerrando
    conectado = True
    btn_conectar.config(text="Desconectar")
    hilo = threading.Thread(target=hilo_red, args=(ent_ip.get().strip(),), daemon=True)
    hilo.start()


def pausar():
    global pausado
    pausado = not pausado
    btn_pausa.config(text="Reanudar" if pausado else "Pausar")


def limpiar():
    rpm.clear()
    duty.clear()
    dibujar()


def enviar(orden):
    """Manda una orden JSON (una linea) al ESP32."""
    if sock is None:
        estado("Sin conexion: no se envio la orden")
        return
    try:
        sock.sendall((json.dumps(orden) + "\n").encode())
    except OSError:
        estado("Error al enviar la orden")


# ---------- GRAFICAS ----------
def estilo_grafica(fig, ax, color):
    """Aspecto del diseno: fondo de tarjeta, sin marco, lineas guia y trazo grueso."""
    fig.patch.set_facecolor(TARJETA)
    ax.set_facecolor(TARJETA)
    for lado in ax.spines.values():
        lado.set_visible(False)
    ax.set_xticks([])
    ax.set_xlim(0, N_PUNTOS - 1)
    ax.grid(axis="y", color=BORDE)
    ax.tick_params(axis="y", length=0, labelsize=8, labelcolor=GRIS)
    fig.subplots_adjust(left=0.07, right=0.99, top=0.93, bottom=0.07)
    linea, = ax.plot([], [], color=color, linewidth=2.5, solid_capstyle="round", clip_on=False)
    return linea


def poner_escala(ax, tope):
    """3 lineas guia: 0, la mitad y el tope."""
    marcas = [0, tope / 2, tope]
    ax.set_ylim(0, tope)
    ax.set_yticks(marcas)
    ax.set_yticklabels([str(int(m)) for m in marcas])


def dibujar():
    linea_rpm.set_data(list(range(len(rpm))), list(rpm))
    linea_duty.set_data(list(range(len(duty))), list(duty))
    poner_escala(ax1, max(max(rpm, default=0), 100))
    canvas1.draw_idle()
    canvas2.draw_idle()


# ---------- MOSTRAR DATOS ----------
def estado(texto):
    """Cambia el mensaje de arriba y su color (verde = conectado)."""
    if texto.startswith("Conectado"):
        fondo, letra = "#D6EEC9", "#2F5420"
    elif texto == "Desconectado":
        fondo, letra = BORDE, GRIS
    else:                                  # error o aviso
        fondo, letra = "#FFD8C0", "#5A2B14"
    lbl_estado.config(text=texto, bg=fondo, fg=letra)


def mostrar(d):
    rpm.append(d.get("rpm", 0))
    duty.append(d.get("pwm_duty", 0))
    v_estado.config(text=str(d.get("fsm_state", "-")))
    v_pwm.config(text="%s %%" % d.get("pwm_duty", "-"))
    v_rpm.config(text=str(d.get("rpm", "-")))
    t_i2c.config(text=str(d.get("i2c_sensor", "Sensor I2C")))
    v_i2c.config(text=str(d.get("val_i2c", "-")))
    dibujar()


def actualizar():
    """Cada 100 ms vacia la cola y actualiza la ventana (la ventana nunca espera la red)."""
    global conectado
    while not cola.empty():
        tipo, valor = cola.get()
        if tipo == "estado":
            estado(valor)
        elif tipo == "fin":
            conectado = False
            btn_conectar.config(text="Conectar")
        elif tipo == "dato" and not pausado:
            mostrar(valor)
    root.after(100, actualizar)


def cerrar():
    global conectado
    conectado = False
    root.destroy()


# ---------- PIEZAS DEL DISENO ----------
def tarjeta(padre, expandir=False):
    t = tk.Frame(padre, bg=TARJETA, padx=18, pady=16)
    t.pack(fill="both" if expandir else "x", expand=expandir, pady=(0, 14))
    return t


def texto(padre, txt, tam, color=TEXTO, negrita=False):
    return tk.Label(padre, text=txt, bg=TARJETA, fg=color, anchor="w",
                    font=(FUENTE, tam, "bold" if negrita else "normal"))


def boton(padre, txt, fondo, letra, comando):
    b = tk.Button(padre, text=txt, bg=fondo, fg=letra, activebackground=fondo,
                  activeforeground=letra, relief="flat", bd=0, cursor="hand2",
                  font=(FUENTE, 11, "bold"), command=comando)
    b.pack(fill="x", pady=5, ipady=8)
    return b


def metrica(col, titulo, color):
    """Tarjeta pequena de arriba: un titulo y un valor grande."""
    t = tk.Frame(fila_metricas, bg=TARJETA, padx=18, pady=16)
    t.grid(row=0, column=col, sticky="nsew", padx=(0 if col == 0 else 14, 0))
    fila_metricas.columnconfigure(col, weight=1, uniform="m")
    etiqueta = texto(t, titulo, 9, GRIS)
    etiqueta.pack(fill="x")
    valor = texto(t, "-", 16, color, True)
    valor.pack(fill="x", pady=(4, 0))
    return etiqueta, valor


def nueva_grafica(titulo, color):
    t = tarjeta(der, expandir=True)
    texto(t, titulo, 10, GRIS, True).pack(fill="x", pady=(0, 6))
    fig = Figure(figsize=(5.8, 1.3), dpi=100)
    ax = fig.add_subplot(111)
    linea = estilo_grafica(fig, ax, color)
    canvas = FigureCanvasTkAgg(fig, master=t)
    canvas.get_tk_widget().configure(bg=TARJETA, highlightthickness=0)
    canvas.get_tk_widget().pack(fill="both", expand=True)
    return ax, linea, canvas


# ---------- VENTANA ----------
root = tk.Tk()
root.title("HMI - Banco de pruebas")
root.configure(bg=PAGINA)
root.geometry("900x740")
root.minsize(780, 660)
root.protocol("WM_DELETE_WINDOW", cerrar)

# --- columna izquierda ---
izq = tk.Frame(root, bg=PAGINA, width=230)
izq.pack(side="left", fill="y", padx=14, pady=14)
izq.pack_propagate(False)

tk.Label(izq, text="Banco de pruebas", bg=PAGINA, fg=TEXTO, anchor="w",
         font=(FUENTE, 16, "bold")).pack(fill="x", pady=(0, 14))

t_conexion = tarjeta(izq)
texto(t_conexion, "IP del ESP32", 10, GRIS).pack(fill="x")
ent_ip = tk.Entry(t_conexion, bg=TARJETA, fg=TEXTO, insertbackground=TEXTO, relief="flat",
                  font=(FUENTE, 11), highlightthickness=2,
                  highlightbackground=BORDE, highlightcolor=AZUL)
ent_ip.insert(0, IP_POR_DEFECTO)
ent_ip.pack(fill="x", ipady=8, pady=(4, 12))
lbl_estado = tk.Label(t_conexion, text="Desconectado", bg=BORDE, fg=GRIS, pady=6,
                      wraplength=190, font=(FUENTE, 10, "bold"))
lbl_estado.pack(fill="x", pady=(0, 6))
btn_conectar = boton(t_conexion, "Conectar", "#FFD8C0", "#5A2B14", conectar_desconectar)
btn_pausa = boton(t_conexion, "Pausar", "#FFEBAE", "#5C4510", pausar)
boton(t_conexion, "Limpiar gráfica", "#CFE6F5", "#1B4766", limpiar)

t_motor = tarjeta(izq)
texto(t_motor, "Motor", 10, GRIS).pack(fill="x", pady=(0, 6))
boton(t_motor, "Encender", "#D6EEC9", "#2F5420", lambda: enviar({"cmd": "ON"}))
boton(t_motor, "Apagar", "#CFE6F5", "#1B4766", lambda: enviar({"cmd": "OFF"}))
boton(t_motor, "Reiniciar", "#FFEBAE", "#5C4510", lambda: enviar({"cmd": "RESET"}))

# --- columna derecha ---
der = tk.Frame(root, bg=PAGINA)
der.pack(side="left", fill="both", expand=True, padx=(0, 14), pady=14)

fila_metricas = tk.Frame(der, bg=PAGINA)
fila_metricas.pack(fill="x", pady=(0, 14))
_, v_estado = metrica(0, "Estado FSM", TEXTO)
_, v_pwm = metrica(1, "PWM", AZUL)
_, v_rpm = metrica(2, "RPM", NARANJA)
t_i2c, v_i2c = metrica(3, "Sensor I2C", TEXTO)

ax1, linea_rpm, canvas1 = nueva_grafica("RPM", NARANJA)
ax2, linea_duty, canvas2 = nueva_grafica("PWM (%) · 1 muestra por segundo", AZUL)
poner_escala(ax1, 100)
poner_escala(ax2, 100)

t_slider = tarjeta(der)
texto(t_slider, "Velocidad PWM (%): se envía al soltar", 10, GRIS).pack(fill="x")
escala = tk.Scale(t_slider, from_=0, to=100, orient="horizontal", bg=TARJETA, fg=TEXTO,
                  troughcolor="#CFE6F5", activebackground=AZUL, highlightthickness=0,
                  bd=0, font=(FUENTE, 10))
escala.set(100)
escala.pack(fill="x", pady=(8, 0))
escala.bind("<ButtonRelease-1>", lambda e: enviar({"cmd": "PWM", "valor": escala.get()}))

actualizar()
root.mainloop()