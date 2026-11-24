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

## L2 protocol

* `src/router.c`, `src/irouter.c`, `src/l2direct.c`: the L2 connection, SGI's Router framing and IRouter packets on TCP 8000, ported from SGI-L2-WUI `backend/functions/{router,irouter,l2-client}.js`.
* `src/l2client.c`: the asynchronous command queue, driven from the Xt event loop.
* `src/net.c`: shared TCP connect / write helpers (also used by the L2 Terminal).

`tests/test_l2proto.c` checks the packets byte for byte against traffic captured from SGI's l2gui.

See [PROTOCOL.md](PROTOCOL.md) for more information.