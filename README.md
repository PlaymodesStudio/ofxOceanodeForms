# ofxOceanodeForms

FORMS / Scroller nodes for ofxOceanode, registered under the `Forms` category:

- **Forms Scroller** – scrolls through numbered PNGs (`N.png`) driven by a phase input
- **Forms Scroller Folder** – scrolls a folder of PNGs and outputs musical data read from `N_bars.txt`, `N_scale.txt`, `N_root.txt`, `N_trans.txt`, `N_range.txt`
- **FORMS P5 PNG Director** – probabilistic element sequencer
- **Texture Unifier Forms**
- **FORMS->P5 OSC Sender** – requires `OFXOCEANODE_USE_OSC`

## Usage

```cpp
#include "ofxOceanodeForms.h"
ofxOceanodeForms::registerModels(oceanode);
```
