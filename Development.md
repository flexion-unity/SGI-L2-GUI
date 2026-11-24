## Development on Linux

Keep the code strict C89. 
For development and testing without an SGI:

```sh
sudo apt install libmotif-dev libxt-dev
make -f Makefile.linux && make -f Makefile.linux test
./sgil2gui -demo
```

## Brick images

`res/bricks/*.png` are the WUI images. 
`tools/png2c.py` scales them to 560 px wide, flattens the alpha onto the rack colour, reduces each to a 256 colour palette and PackBits-compresses it into `src/img/img_*.c`.

After changing a PNG:

```sh
python3 tools/png2c.py res/bricks src/img
```

Overlay positions (LCD, LEDs, label) are in `src/skins.c`
