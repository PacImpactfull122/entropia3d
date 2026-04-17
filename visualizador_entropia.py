import numpy as np
import matplotlib
matplotlib.use("TkAgg")
import matplotlib.pyplot as plt
import matplotlib.animation as animation
import matplotlib.gridspec as gridspec
from matplotlib.colors import LinearSegmentedColormap
from scipy.ndimage import uniform_filter1d
import json, os, collections

ARQUIVO_DADOS = "/tmp/entropia_data.json"
HIST_LEN      = 120
RES_X         = 32
RES_Y         = 32
MAX_HIST_METR = 200

historico    = collections.defaultdict(lambda: collections.deque(maxlen=HIST_LEN))
hist_miss_l1 = collections.defaultdict(lambda: collections.deque(maxlen=MAX_HIST_METR))
hist_miss_l2 = collections.defaultdict(lambda: collections.deque(maxlen=MAX_HIST_METR))
hist_branch  = collections.defaultdict(lambda: collections.deque(maxlen=MAX_HIST_METR))
hist_stall   = collections.defaultdict(lambda: collections.deque(maxlen=MAX_HIST_METR))
hist_ipc     = collections.defaultdict(lambda: collections.deque(maxlen=MAX_HIST_METR))

num_nucleos_real = [0]
fase             = [0.0]
info_cpu         = [{}]

CORES_OCEANO = [
    (0.00, "#03001C"), (0.10, "#0a0a3a"), (0.22, "#0d3b6e"),
    (0.38, "#0077b6"), (0.52, "#00b4d8"), (0.65, "#90e0ef"),
    (0.80, "#caf0f8"), (0.92, "#ffe8d6"), (1.00, "#ffffff"),
]
cmap_oceano = LinearSegmentedColormap.from_list("oceano", [(v, c) for v, c in CORES_OCEANO])

COR_FUNDO = "#03001C"
COR_TEXTO = "#4488aa"
COR_EIXO  = "#2a6a9a"
COR_GRADE = "#0d1b2a"

fig = plt.figure(figsize=(18, 10), facecolor=COR_FUNDO)
gs  = gridspec.GridSpec(2, 3, figure=fig,
                        left=0.05, right=0.97,
                        top=0.93, bottom=0.07,
                        wspace=0.38, hspace=0.45)

ax_surf   = fig.add_subplot(gs[0, 0], projection="3d", facecolor=COR_FUNDO)
ax_heat   = fig.add_subplot(gs[0, 1], facecolor=COR_FUNDO)
ax_cache  = fig.add_subplot(gs[0, 2], facecolor=COR_FUNDO)
ax_ipc    = fig.add_subplot(gs[1, 0], facecolor=COR_FUNDO)
ax_branch = fig.add_subplot(gs[1, 1], facecolor=COR_FUNDO)
ax_info   = fig.add_subplot(gs[1, 2], facecolor=COR_FUNDO)

ax_surf.view_init(elev=28, azim=-60)
ax_surf.set_box_aspect([2.0, 1.5, 1.2])

titulo = fig.suptitle("entropia cpu", color=COR_TEXTO, fontsize=12, y=0.98, fontweight="bold")

_xg = np.linspace(0.0, 1.0, RES_X)
_yg = np.linspace(0.0, 1.0, RES_Y)
XN, YN = np.meshgrid(_xg, _yg)

cbar_ref = [None]


def _fmt_bytes(b):
    if b == 0:
        return "?"
    if b >= 1048576:
        return "%dmb" % (b // 1048576)
    if b >= 1024:
        return "%dkb" % (b // 1024)
    return "%db" % b


def _estilo_ax2d(ax, titulo_ax):
    ax.set_facecolor(COR_FUNDO)
    ax.tick_params(colors=COR_EIXO, labelsize=7)
    ax.set_title(titulo_ax, color=COR_TEXTO, fontsize=8, pad=4)
    for sp in ax.spines.values():
        sp.set_edgecolor(COR_GRADE)
    ax.xaxis.label.set_color(COR_EIXO)
    ax.yaxis.label.set_color(COR_EIXO)


def ler_dados():
    if not os.path.exists(ARQUIVO_DADOS):
        return None
    try:
        with open(ARQUIVO_DADOS, "r") as f:
            raw = f.read().strip()
        return json.loads(raw) if raw else None
    except Exception:
        return None


def construir_Z(n_total):
    ids = sorted(historico.keys())
    if not ids:
        campo = (
            0.18 * np.sin(XN * 12.57 + fase[0] * 1.1) * np.cos(YN * 6.28 + fase[0] * 0.7)
            + 0.14 * np.sin(XN * 18.85 + YN * 12.57 + fase[0] * 1.4)
        )
        cmin, cmax = campo.min(), campo.max()
        campo = (campo - cmin) / (cmax - cmin + 1e-9) * 0.4
        return XN * max(n_total, 1), YN * HIST_LEN, campo.astype(np.float32)

    n_ids = len(ids)
    hist  = np.zeros((HIST_LEN, n_ids), dtype=np.float32)
    for j, nid in enumerate(ids):
        vals = np.array(list(historico[nid]), dtype=np.float32)
        if vals.size > 0:
            hist[HIST_LEN - vals.size:, j] = vals

    hist = uniform_filter1d(hist, size=5, axis=0)
    x_reais = np.linspace(0.0, 1.0, n_ids) if n_ids > 1 else np.array([0.5])
    Z = np.zeros((RES_Y, RES_X), dtype=np.float32)
    for r in range(RES_Y):
        idx  = int(r / (RES_Y - 1) * (HIST_LEN - 1))
        Z[r] = np.interp(_xg, x_reais, hist[idx])

    Z = uniform_filter1d(Z, size=3, axis=1)
    np.clip(Z, 0.0, 1.0, out=Z)
    return XN * max(n_total, n_ids, 1), YN * HIST_LEN, Z


def _desenhar_surf(n_total):
    ax_surf.cla()
    ax_surf.set_facecolor(COR_FUNDO)
    ax_surf.view_init(elev=28, azim=-60)
    ax_surf.set_box_aspect([2.0, 1.5, 1.2])
    for pane in [ax_surf.xaxis.pane, ax_surf.yaxis.pane, ax_surf.zaxis.pane]:
        pane.fill = False
        pane.set_edgecolor(COR_GRADE)
    ax_surf.tick_params(colors=COR_GRADE, labelsize=6)
    ax_surf.set_xlabel("nucleo",   color=COR_EIXO,  labelpad=8, fontsize=8)
    ax_surf.set_ylabel("tempo",    color=COR_EIXO,  labelpad=8, fontsize=8)
    ax_surf.set_zlabel("entropia", color=COR_TEXTO, labelpad=6, fontsize=8)
    ax_surf.set_zlim(0.0, 1.0)
    ax_surf.set_title("entropia 3d", color=COR_TEXTO, fontsize=8, pad=4)

    X, Y, Z = construir_Z(n_total)
    s = ax_surf.plot_surface(X, Y, Z, cmap=cmap_oceano, linewidth=0,
                              antialiased=False, vmin=0.0, vmax=1.0,
                              alpha=0.93, rcount=RES_Y, ccount=RES_X)
    if cbar_ref[0] is None:
        cbar_ref[0] = fig.colorbar(s, ax=ax_surf, shrink=0.28, pad=0.08, aspect=18)
        cbar_ref[0].set_label("shannon", color=COR_TEXTO, fontsize=7)
        plt.setp(cbar_ref[0].ax.yaxis.get_ticklabels(), color=COR_EIXO, fontsize=6)
        cbar_ref[0].set_ticks([0.0, 0.5, 1.0])

    if n_total > 0:
        n_ticks = min(n_total, 6)
        ax_surf.set_xticks(np.linspace(0, max(n_total - 1, 1), n_ticks))
        ax_surf.set_xticklabels(
            ["c%d" % int(i) for i in np.linspace(0, n_total - 1, n_ticks)],
            color=COR_EIXO, fontsize=6)
    return float(np.max(Z)), float(np.mean(Z))


def _desenhar_heatmap(ids):
    ax_heat.cla()
    _estilo_ax2d(ax_heat, "heatmap entropia por nucleo")
    if not ids:
        return
    n      = len(ids)
    matriz = np.zeros((n, HIST_LEN), dtype=np.float32)
    for i, nid in enumerate(ids):
        vals = np.array(list(historico[nid]), dtype=np.float32)
        if vals.size > 0:
            matriz[i, HIST_LEN - vals.size:] = vals
    ax_heat.imshow(matriz, aspect="auto", cmap=cmap_oceano,
                   vmin=0.0, vmax=1.0, origin="lower", interpolation="nearest")
    ax_heat.set_yticks(range(n))
    ax_heat.set_yticklabels(["c%d" % nid for nid in ids], fontsize=6, color=COR_EIXO)
    ax_heat.set_xlabel("amostras", fontsize=7)
    ax_heat.set_ylabel("nucleo",   fontsize=7)


def _desenhar_cache(ids):
    ax_cache.cla()
    _estilo_ax2d(ax_cache, "cache misses l1 e l2")
    if not ids:
        return
    for nid in ids:
        if hist_miss_l1[nid]:
            t = np.arange(len(hist_miss_l1[nid]))
            ax_cache.plot(t, list(hist_miss_l1[nid]),
                          linewidth=0.8, label="l1 c%d" % nid, alpha=0.85)
        if hist_miss_l2[nid]:
            t = np.arange(len(hist_miss_l2[nid]))
            ax_cache.plot(t, list(hist_miss_l2[nid]),
                          linewidth=0.8, linestyle="--", label="l2 c%d" % nid, alpha=0.7)
    ax_cache.set_xlabel("amostras", fontsize=7)
    ax_cache.set_ylabel("misses",   fontsize=7)
    if len(ids) <= 4:
        ax_cache.legend(fontsize=5, facecolor=COR_FUNDO,
                        edgecolor=COR_GRADE, labelcolor=COR_EIXO, loc="upper left")


def _desenhar_ipc(ids):
    ax_ipc.cla()
    _estilo_ax2d(ax_ipc, "ipc estimado por nucleo")
    if not ids:
        return
    for nid in ids:
        if hist_ipc[nid]:
            t = np.arange(len(hist_ipc[nid]))
            ax_ipc.plot(t, list(hist_ipc[nid]),
                        linewidth=0.9, label="c%d" % nid, alpha=0.9)
    ax_ipc.set_xlabel("amostras", fontsize=7)
    ax_ipc.set_ylabel("ipc",      fontsize=7)
    ax_ipc.set_ylim(bottom=0)
    if len(ids) <= 6:
        ax_ipc.legend(fontsize=5, facecolor=COR_FUNDO,
                      edgecolor=COR_GRADE, labelcolor=COR_EIXO, loc="upper left")


def _desenhar_branch(ids):
    ax_branch.cla()
    _estilo_ax2d(ax_branch, "branch miss e stall cycles")
    if not ids:
        return
    for nid in ids:
        if hist_branch[nid]:
            t = np.arange(len(hist_branch[nid]))
            ax_branch.plot(t, list(hist_branch[nid]),
                           linewidth=0.8, label="branch c%d" % nid, alpha=0.85)
        if hist_stall[nid]:
            t = np.arange(len(hist_stall[nid]))
            ax_branch.plot(t, list(hist_stall[nid]),
                           linewidth=0.8, linestyle=":", label="stall c%d" % nid, alpha=0.7)
    ax_branch.set_xlabel("amostras", fontsize=7)
    ax_branch.set_ylabel("contagem", fontsize=7)
    if len(ids) <= 4:
        ax_branch.legend(fontsize=5, facecolor=COR_FUNDO,
                         edgecolor=COR_GRADE, labelcolor=COR_EIXO, loc="upper left")


def _desenhar_info():
    ax_info.cla()
    ax_info.set_facecolor(COR_FUNDO)
    ax_info.axis("off")
    ax_info.set_title("info cpu", color=COR_TEXTO, fontsize=8, pad=4)

    inf = info_cpu[0]
    if not inf:
        ax_info.text(0.5, 0.5, "aguardando dados", color=COR_EIXO,
                     ha="center", va="center", fontsize=8,
                     transform=ax_info.transAxes)
        return

    linhas = [
        ("fabricante : %s" % inf.get("fabricante", "?"),                          COR_TEXTO),
        ("marca      : %.28s" % inf.get("marca", "?"),                             COR_EIXO),
        ("familia %s  modelo %s  step %s" % (
            inf.get("familia", "?"), inf.get("modelo", "?"),
            inf.get("stepping", "?")),                                             COR_EIXO),
        ("nucleos    : %s fis  %s log" % (
            inf.get("nucleos", "?"), inf.get("nucleos_logicos", "?")),             COR_EIXO),
        ("freq base  : %s mhz  boost %s mhz" % (
            inf.get("freq_base", 0), inf.get("freq_boost", 0)),                   COR_EIXO),
        ("l1d %s  l1i %s  l2 %s  l3 %s" % (
            _fmt_bytes(inf.get("cache_l1d", 0)), _fmt_bytes(inf.get("cache_l1i", 0)),
            _fmt_bytes(inf.get("cache_l2",  0)), _fmt_bytes(inf.get("cache_l3",  0))), COR_EIXO),
        ("contadores pmu : %s" % inf.get("contadores", "?"),                      COR_EIXO),
    ]

    nomes_feat = [
        "sse", "sse2", "sse3", "ssse3", "sse41", "sse42",
        "avx", "avx2", "avx512f", "aes", "rdrand", "rdseed",
        "bmi1", "bmi2", "tsx",
    ]
    feat_linha = "  ".join(
        n if inf.get(n, 0) else ("." * len(n))
        for n in nomes_feat
    )

    y     = 0.97
    passo = 0.115
    for texto, cor in linhas:
        ax_info.text(0.03, y, texto, color=cor, fontsize=6.5,
                     transform=ax_info.transAxes, va="top",
                     fontfamily="monospace")
        y -= passo

    ax_info.text(0.03, y, feat_linha, color="#00b4d8", fontsize=5.5,
                 transform=ax_info.transAxes, va="top", fontfamily="monospace")


def atualizar(frame):
    dados = ler_dados()
    if dados is not None:
        n = int(dados.get("nucleos", 0))
        if n > 0:
            num_nucleos_real[0] = n
        info_cpu[0] = dados

        for cel in dados.get("celulas", []):
            nid    = int(cel["nucleo"])
            ciclos = int(cel.get("ciclos",      0))
            instr  = int(cel.get("instrucoes",  0))

            historico[nid].append(float(cel["entropia"]))

            # * ipc calculado como instrucoes por ciclo, evita divisao por zero
            if ciclos > 0:
                hist_ipc[nid].append(instr / ciclos)

            hist_miss_l1[nid].append(int(cel.get("miss_l1",    0)))
            hist_miss_l2[nid].append(int(cel.get("miss_l2",    0)))
            hist_branch[nid].append(int(cel.get("branch_miss", 0)))
            hist_stall[nid].append(int(cel.get("stall",        0)))

    fase[0] += 0.06
    n_total  = max(num_nucleos_real[0], len(historico))
    ids      = sorted(historico.keys())

    max_v, med_v = _desenhar_surf(n_total)
    _desenhar_heatmap(ids)
    _desenhar_cache(ids)
    _desenhar_ipc(ids)
    _desenhar_branch(ids)
    _desenhar_info()

    amostras = max((len(v) for v in historico.values()), default=0)
    titulo.set_text(
        "entropia cpu  |  %d nucleos  |  %d amostras  |  max %.3f  media %.3f"
        % (n_total, amostras, max_v, med_v)
    )


ani = animation.FuncAnimation(fig, atualizar, interval=16,
                               blit=False, cache_frame_data=False)
plt.show()
