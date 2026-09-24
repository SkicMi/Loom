#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Generira FontData.h za Treadle.

Odabrani TrueType font smanjuje se u atlas R8 (samo pokrivenost) i mrezu
fiksiranih celija, a uz njega se upisuju sirine, ispune i polozaj tinte svakog
znaka. Treadle iz tog zapisa racuna tekst PA RAZMJERNO: visina retka je uvijek
7 logickih jedinica (to je sucelje koje raspored vec koristi), a sve mjere se
dijele s atlasScale = linePx / 7.

Pokrenuti:  python3 tools/ui/make_font.py
Rezultat:   treadle/src/Treadle/FontData.h  (predan u repo, izvor nije potreban)
"""

import io
import os
import sys

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
HEADER_PATH = os.path.join(HERE, "..", "..", "treadle", "src", "Treadle", "FontData.h")

#Podebljano lice i gotovo dvostruko veca rezolucija od zadane pocetne verzije: tekst na
#zaslonu dobije GUSTE, CVRSTE crte (bold) i glatki rub (vise atlas piksela po pikselu
#zaslona), pa izgleda "otisnut", ne svijetlo i otrcano. Izbor tezine ne mijenja nista u
#Treadleu - metrike se mjere iz rastera, pa se sve uskladi samo od sebe.
FONT_PATH = "/usr/share/fonts/truetype/noto/NotoSans-Bold.ttf"

FIRST = ord(" ")
LAST = ord("~")          # ukljucivo
LINE_PX_DEFAULT = 80     # sazeto u visini retka; prava se racuna iz fonta (vidi dolje)
CELL = 160               # velicina celije; unutrasnjost (m. margine) mora stati najstrji znak u oba smjera
PAD = 12                 # margina tinte od ruba celije, MORa pokriti najveci negativni ispun (Noto Sans
                         # ima do ~-5 px: 'J','j',... sto se neko slovo ne uvuce u susjednu celiju)
COLS = 16
ROWS = (LAST - FIRST + 1 + COLS - 1) // COLS
ATLAS_W = COLS * CELL
ATLAS_H = ROWS * CELL


def main():
    if not os.path.exists(FONT_PATH):
        sys.exit("nema fonta: " + FONT_PATH)
    font = ImageFont.truetype(FONT_PATH, LINE_PX_DEFAULT)
    ascent, descent = font.getmetrics()

    # Cijeli em-retak (ascent + descent) odlucuje o brojcanom razmjeru, ne o LINE_PX_DEFAULT:
    # Noto Sans ima velike produke i petlje ih ne smiju prebrisati. Visina retka sucelja je
    # 7 logickih jedinica, pa je atlasScale = linePx / 7 at so i ostaje tocan za svaki znak.
    line_px = ascent + descent
    atlas_scale = line_px // 7

    atlas = Image.new("L", (ATLAS_W, ATLAS_H), 0)
    draw = ImageDraw.Draw(atlas)
    advances = []

    for i in range(FIRST, LAST + 1):
        char = chr(i)
        advances.append(font.getlength(char))
        row = (i - FIRST) // COLS
        col = (i - FIRST) % COLS
        cell_x = col * CELL + PAD
        cell_y = row * CELL + PAD
        # SIDRO "la" postavlja vrh em retka na (cell_x, cell_y): tinta svakog znaka pocinje na
        # istoj polaznoj crti, descenderi ne izlaze iz retka, a razmak crta je atlasScale piksela.
        draw.text((cell_x, cell_y), char, font=font, fill=255, anchor="la")

    # Metrike se mjere IZ SAMOG ATLASA, ne iz fonta. Oblik moze pomaknuti tintu za cijeli
    # piksel (npr. usklicnik u Noto Sansu), i tada getbbox iz liba ne bi odgovarao onome sto
    # je zaista nacrtano. Kako i geometrija i UV racunaju iz tih brojeva, a oni iz istog
    # rastera, polozaj i oblik slova ne mogu se raziici. Prag 32 odbacuje antihaliasnu kosinu
    # ruba tako da mjerenje vidi cvrstu tintu, a ne zamrljanost.
    THRESH = 32

    metrics = []  # (advance, bearing, inkW, inkH, topFromLineTop, cellIndex) - sve u PIKELIMA ATLASA
    for i in range(FIRST, LAST + 1):
        row = (i - FIRST) // COLS
        col = (i - FIRST) % COLS
        # Mjeri se CIJELA celija, ne samo unutrasnjost: negativan ispun ('J','j',...) gurne tintu u
        # lijevi rub celije. PAD osigurava da se tinta nikad ne prelije u SUSJEDNU celiju, pa je
        # okvir mjeren iz ejog stabla istinit i ne zahvaca tude slovo.
        crop = atlas.crop((col * CELL, row * CELL,
                           (col + 1) * CELL, (row + 1) * CELL))
        found = crop.point(lambda v: 1 if v > THRESH else 0).getbbox()
        if found is None:
            metrics.append((advances[i - FIRST], 0, 0, 0, 0, i - FIRST))
        else:
            left, topy, right, bottomy = found
            # Polozaj olovke je cell_x + PAD (pa je em-vrh na cell_y + PAD): odbijanjem PAD-a
            # dobivamo pravi ispun/vrh isti kao u fontu - a UV se racuna natrag + PAD.
            metrics.append((advances[i - FIRST], left - PAD, right - left,
                            bottomy - topy, topy - PAD, i - FIRST))

    # Metrike su mjerene iz celija, pa su tocno uskladene s rasterom; ostaje jos samo paziti da
    # se tinte susjednih celija ne preliju jedna u drugu - to bi linearni filter vukao susjedno
    # slovo (a brojevi iz mjerenja bili bi krivi).
    for i, (advance, bearing, ink_w, ink_h, top, cell) in enumerate(metrics):
        if ink_w <= 0 or ink_h <= 0:
            continue
        if (bearing < -PAD or top < -PAD or bearing + ink_w > CELL - PAD or top + ink_h > CELL - PAD):
            sys.exit("provjera %s: tinta izlazi iz celije "
                     "(bearing %d, top %d, %dx%d)" % (chr(FIRST + i), bearing, top, ink_w, ink_h))
    print("provjera atlasa: sva %d slova stane u svoju celiju" % sum(1 for m in metrics if m[2] > 0))

    # Zapis header-a.
    pixels = atlas.tobytes()

    out = []
    out.append("//GENERIRANO: tools/ui/make_font.py. Ne mijenjaj rucno.")
    out.append("//")
    out.append("//Atlas R8 pokrivenosti (nema boje - tekst se boji u shaderu) i metrike svakog")
    out.append("//znaka 32..126. Tinta svakog znaka stoji u svojoj celiji s %d px margine, pa" % PAD)
    out.append("//linearno filtriranje na rubu tinte gleda u prazno, a ne u susjedno slovo.")
    out.append("//")
    out.append("//Visina retka je %d atlas piksela i znaci 7 logickih jedinica sucelja," % line_px)
    out.append("//pa je jedan logicki piksel = atlasScale = %d atlas piksela." % atlas_scale)
    out.append("#pragma once")
    out.append("#include <cstdint>")
    out.append("namespace Treadle{")
    out.append("")
    out.append("    constexpr int  kFontFirst = %d;" % FIRST)
    out.append("    constexpr int  kFontLast  = %d;" % LAST)
    out.append("    constexpr int  kFontLinePx = %d;      // visina retka u atlas pikselima" % line_px)
    out.append("    constexpr int  kFontAtlasScale = %d;  // logicki piksel = ovo atlas piksela" % atlas_scale)
    out.append("    constexpr int  kFontAtlasWidth = %d;" % ATLAS_W)
    out.append("    constexpr int  kFontAtlasHeight = %d;" % ATLAS_H)
    out.append("    constexpr int  kFontCellPx = %d;  // celija (ukljucujuci marginu)" % CELL)
    out.append("    constexpr int  kFontPadPx  = %d;  // margina tinte od ruba celije" % PAD)
    out.append("    constexpr int  kFontCols   = %d;" % COLS)
    out.append("    //Pokrivenost svakog znaka, po jedan bajt po pikselu, red po red")
    out.append("    const unsigned char kFontAtlasPixels[] = {")
    for y in range(ATLAS_H):
        row_bytes = pixels[y * ATLAS_W:(y + 1) * ATLAS_W]
        chunks = ", ".join(str(b) for b in row_bytes)
        out.append("        %s," % chunks)
    out.append("    };")
    out.append("")
    out.append("    //Jedan znak: metrike u ATLAS PIKELIMA, dijele se s kFontAtlasScale u logickom racunu")
    out.append("    struct FontGlyphData{")
    out.append("        uint16_t advancePx;   // korak olovke")
    out.append("        int16_t  bearingPx;    // od mjesta olovke do lijeve tinte (moze biti negativno)")
    out.append("        uint16_t topPx;       // vrha retka do vrha tinte")
    out.append("        uint16_t inkWidthPx;")
    out.append("        uint16_t inkHeightPx;")
    out.append("        uint16_t cell;        // broj celije u atlasu")
    out.append("    };")
    out.append("")
    out.append("    //Za svaki znak kFontFirst..kFontLast, redom po ASCII kodu")
    out.append("    inline const FontGlyphData kFontGlyphs[] = {")
    for advance, bearing, ink_w, ink_h, top, cell in metrics:
        out.append("        {%d, %d, %d, %d, %d, %d}," %
                   (int(round(advance)), int(bearing), int(top),
                    int(ink_w), int(ink_h), int(cell)))
    out.append("    };")
    out.append("    constexpr int kFontGlyphCount = int(kFontLast - kFontFirst + 1);")
    out.append("")
    out.append("}")
    out.append("")

    with open(HEADER_PATH, "w") as f:
        f.write("\n".join(out))
    print("upisano %d znakova, atlas %dx%d, %d kB (retak %d px, razmjer %d) u %s" %
          (LAST - FIRST + 1, ATLAS_W, ATLAS_H, len(pixels) // 1024, line_px, atlas_scale, HEADER_PATH))


if __name__ == "__main__":
    main()