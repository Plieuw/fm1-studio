#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# STUDIO: alles in één keer klaarzetten en de firmware bouwen, op Linux (Ubuntu / Debian / Linux Mint, of WSL op Windows).
#
#   sh studio-setup.sh            installeert wat nodig is (vraagt één keer je wachtwoord voor apt) en bouwt
#   sh studio-setup.sh --build    alleen opnieuw bouwen (na een wijziging)
#   sh studio-setup.sh --install  de installatiepagina starten: open daarna http://localhost:8000/webapp/installer/
#                                 in Chrome of Edge (ook op Windows als je in WSL werkt) en klik Install
#
# Resultaat: build/felucca.fwsc  (het bestand dat je op de FM-1 zet; zie LEESMIJ.md, stap 4)
set -e
cd "$(dirname "$0")"
TC="${JIELI_TOOLCHAIN:-$HOME/.jieli/toolchain}"
SDK="${AC79_SDK:-$HOME/fw-AC79_AIoT_SDK}"

say() { printf '\n\033[1;32m==> %s\033[0m\n' "$1"; }
die() { printf '\n\033[1;31mFOUT: %s\033[0m\n' "$1"; exit 1; }

[ "$(uname -s)" = Linux ] || die "dit script is voor Linux (of WSL op Windows). Op een Mac: zie BUILDING.md (Docker)."
[ "$(uname -m)" = x86_64 ] || die "de JieLi-compiler draait alleen op een 64-bit Intel/AMD-pc (x86_64); dit is $(uname -m)."

if [ "$1" = "--install" ]; then
    [ -f build/felucca.fwsc ] || die "er is nog geen build/felucca.fwsc: draai eerst  sh studio-setup.sh"
    SITE="${TMPDIR:-/tmp}/studio-site"
    rm -rf "$SITE"
    python3 web/make_site.py build/felucca.fwsc studio "$SITE"
    say "Installatiepagina draait. Open in Chrome of Edge:  http://localhost:8000/webapp/installer/"
    echo "(sluit dit venster of druk Ctrl+C als je klaar bent)"
    cd "$SITE" && exec python3 -m http.server 8000 --bind 127.0.0.1
fi

if [ "$1" != "--build" ]; then
    say "1/3  Programma's installeren (python3, Pillow, fonttools, git, curl)"
    if command -v apt-get >/dev/null 2>&1; then
        sudo apt-get update
        sudo apt-get install -y python3 python3-pil python3-fonttools git curl xz-utils
    else
        echo "Geen apt gevonden: installeer zelf python3, Pillow, fonttools, git, curl en xz, en draai dit script opnieuw."
    fi

    say "2/3  De JieLi-compiler downloaden (eenmalig, ~100 MB)"
    if [ -x "$TC/pi32v2/bin/clang" ]; then
        echo "al aanwezig: $TC"
    else
        mkdir -p "$(dirname "$TC")"
        sh tools/get_toolchain.sh "$(dirname "$TC")"
    fi

    say "3/3  Drie bestanden uit de JieLi AC79 SDK ophalen (eenmalig)"
    if [ -f "$SDK/cpu/wl82/tools/uboot.boot" ]; then
        echo "al aanwezig: $SDK"
    else
        URL=https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git
        BR=AC79NN_SDK_V1.2.1_2023-12-13
        if ! { git clone --depth 1 --filter=blob:none --sparse --branch "$BR" "$URL" "$SDK" &&
               (cd "$SDK" && git sparse-checkout set cpu/wl82/tools); }; then
            echo "(alleen die map ophalen lukte niet: dan de hele SDK, dat duurt langer)"
            rm -rf "$SDK"
            git clone --depth 1 --branch "$BR" "$URL" "$SDK"
        fi
        [ -f "$SDK/cpu/wl82/tools/uboot.boot" ] || die "de SDK-bestanden zijn niet binnengekomen (gitee.com bereikbaar?)"
    fi
fi

say "Firmware bouwen"
chmod +x build.sh tools/*.sh tools/*.py 2>/dev/null || true
export JIELI_TOOLCHAIN="$TC" AC79_SDK="$SDK"
./build.sh
[ -f build/felucca.fwsc ] || die "het bouwen gaf geen build/felucca.fwsc; kijk naar de foutmelding hierboven."
say "Klaar: $(pwd)/build/felucca.fwsc  ($(wc -c < build/felucca.fwsc) bytes)"
echo "Zet hem op de FM-1 met stap 4 in LEESMIJ.md."
