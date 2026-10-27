"""RPG Maker MZ map data, the way the editor writes it: tile ids, ground autotiles that join their neighbours, and
a picture of a map drawn from its tileset images, for looking at a generated map without opening the editor."""
import json
import os

from PIL import Image

TILE_SIZE = 48
TILE_ID_E = 768
TILE_ID_A5 = 1536
TILE_ID_A1 = 2048
TILE_ID_A2 = 2816
LAYERS = 6  # four tile layers, then shadows, then regions

# Tilemap.FLOOR_AUTOTILE_TABLE from rmmz_core.js: for each of the 48 shapes, where its four quarters (top left, top
# right, bottom left, bottom right) come from in the autotile's block, in half-tile steps.
FLOOR_AUTOTILE_TABLE = [
    [[2, 4], [1, 4], [2, 3], [1, 3]], [[2, 0], [1, 4], [2, 3], [1, 3]], [[2, 4], [3, 0], [2, 3], [1, 3]],
    [[2, 0], [3, 0], [2, 3], [1, 3]], [[2, 4], [1, 4], [2, 3], [3, 1]], [[2, 0], [1, 4], [2, 3], [3, 1]],
    [[2, 4], [3, 0], [2, 3], [3, 1]], [[2, 0], [3, 0], [2, 3], [3, 1]], [[2, 4], [1, 4], [2, 1], [1, 3]],
    [[2, 0], [1, 4], [2, 1], [1, 3]], [[2, 4], [3, 0], [2, 1], [1, 3]], [[2, 0], [3, 0], [2, 1], [1, 3]],
    [[2, 4], [1, 4], [2, 1], [3, 1]], [[2, 0], [1, 4], [2, 1], [3, 1]], [[2, 4], [3, 0], [2, 1], [3, 1]],
    [[2, 0], [3, 0], [2, 1], [3, 1]], [[0, 4], [1, 4], [0, 3], [1, 3]], [[0, 4], [3, 0], [0, 3], [1, 3]],
    [[0, 4], [1, 4], [0, 3], [3, 1]], [[0, 4], [3, 0], [0, 3], [3, 1]], [[2, 2], [1, 2], [2, 3], [1, 3]],
    [[2, 2], [1, 2], [2, 3], [3, 1]], [[2, 2], [1, 2], [2, 1], [1, 3]], [[2, 2], [1, 2], [2, 1], [3, 1]],
    [[2, 4], [3, 4], [2, 3], [3, 3]], [[2, 4], [3, 4], [2, 1], [3, 3]], [[2, 0], [3, 4], [2, 3], [3, 3]],
    [[2, 0], [3, 4], [2, 1], [3, 3]], [[2, 4], [1, 4], [2, 5], [1, 5]], [[2, 0], [1, 4], [2, 5], [1, 5]],
    [[2, 4], [3, 0], [2, 5], [1, 5]], [[2, 0], [3, 0], [2, 5], [1, 5]], [[0, 4], [3, 4], [0, 3], [3, 3]],
    [[2, 2], [1, 2], [2, 5], [1, 5]], [[0, 2], [1, 2], [0, 3], [1, 3]], [[0, 2], [1, 2], [0, 3], [3, 1]],
    [[2, 2], [3, 2], [2, 3], [3, 3]], [[2, 2], [3, 2], [2, 1], [3, 3]], [[2, 4], [3, 4], [2, 5], [3, 5]],
    [[2, 0], [3, 4], [2, 5], [3, 5]], [[0, 4], [1, 4], [0, 5], [1, 5]], [[0, 4], [3, 0], [0, 5], [1, 5]],
    [[0, 2], [3, 2], [0, 3], [3, 3]], [[0, 2], [1, 2], [0, 5], [1, 5]], [[0, 4], [3, 4], [0, 5], [3, 5]],
    [[2, 2], [3, 2], [2, 5], [3, 5]], [[0, 2], [3, 2], [0, 5], [3, 5]], [[0, 0], [1, 0], [0, 1], [1, 1]],
]

# Where each quarter comes from, by how it meets its neighbours: the two edge neighbours on its side, and the corner.
QUARTER_PIECES = {
    "tl": {"inside": (2, 4), "inner": (2, 0), "side": (0, 4), "end": (2, 2), "corner": (0, 2)},
    "tr": {"inside": (1, 4), "inner": (3, 0), "side": (3, 4), "end": (1, 2), "corner": (3, 2)},
    "bl": {"inside": (2, 3), "inner": (2, 1), "side": (0, 3), "end": (2, 5), "corner": (0, 5)},
    "br": {"inside": (1, 3), "inner": (3, 1), "side": (3, 3), "end": (1, 5), "corner": (3, 5)},
}
SHAPE_OF_QUARTERS = {tuple(tuple(q) for q in quarters): shape for shape, quarters in enumerate(FLOOR_AUTOTILE_TABLE)}


def quarter_piece(quarter, upright, sideways, diagonal):
    """The piece one quarter of an autotile takes: upright, sideways and diagonal say whether the neighbour above or
    below, beside, and on the corner is the same ground."""
    pieces = QUARTER_PIECES[quarter]
    if upright and sideways:
        return pieces["inside"] if diagonal else pieces["inner"]
    if upright:
        return pieces["side"]
    if sideways:
        return pieces["end"]
    return pieces["corner"]


def autotile_shape(same):
    """The shape (0 to 47) of a ground autotile, given same(dx, dy): whether the neighbour that way is the same ground."""
    n, s, w, e = same(0, -1), same(0, 1), same(-1, 0), same(1, 0)
    quarters = (
        quarter_piece("tl", n, w, same(-1, -1)), quarter_piece("tr", n, e, same(1, -1)),
        quarter_piece("bl", s, w, same(-1, 1)), quarter_piece("br", s, e, same(1, 1)),
    )
    return SHAPE_OF_QUARTERS[quarters]


def autotile_kind(tile_id):
    """The autotile kind of a tile id from A1 or A2, or None for any other tile."""
    if TILE_ID_A1 <= tile_id < TILE_ID_A2 + 32 * 48:
        return (tile_id - TILE_ID_A1) // 48
    return None


def a1_tile(kind, shape=0):
    return TILE_ID_A1 + kind * 48 + shape


def a2_tile(kind, shape=0):
    return TILE_ID_A2 + kind * 48 + shape


def e_tile(index):
    """A tile of the E sheet, by its number as the editor counts it (left half first, then the right half)."""
    return TILE_ID_E + index


class MapGrid:
    """A map's tiles: width x height cells on six layers, flattened the way MapXXX.json keeps them."""

    def __init__(self, width, height):
        self.width = width
        self.height = height
        self.data = [0] * (width * height * LAYERS)

    def index(self, x, y, z):
        return (z * self.height + y) * self.width + x

    def inside(self, x, y):
        return 0 <= x < self.width and 0 <= y < self.height

    def get(self, x, y, z):
        return self.data[self.index(x, y, z)]

    def set(self, x, y, z, tile_id):
        self.data[self.index(x, y, z)] = tile_id

    def join_autotiles(self, z):
        """Gives every A1/A2 autotile on layer z the shape that joins it to its neighbours of the same kind; the map's
        edge counts as the same ground, as in the editor."""
        kinds = [[autotile_kind(self.get(x, y, z)) for x in range(self.width)] for y in range(self.height)]
        for y in range(self.height):
            for x in range(self.width):
                kind = kinds[y][x]
                if kind is None:
                    continue

                def same(dx, dy, x=x, y=y, kind=kind):
                    nx, ny = x + dx, y + dy
                    return not self.inside(nx, ny) or kinds[ny][nx] == kind

                base = self.get(x, y, z) - (self.get(x, y, z) - TILE_ID_A1) % 48
                self.set(x, y, z, base + autotile_shape(same))


def render(grid, tileset_names, image_folder, scale=1.0):
    """A picture of the map's four tile layers, drawn from the tileset's images (ground autotiles, A5 and B to E)."""
    sheets = {}

    def sheet(slot):
        if slot not in sheets:
            name = tileset_names[slot]
            sheets[slot] = Image.open(os.path.join(image_folder, name + ".png")).convert("RGBA") if name else None
        return sheets[slot]

    half = TILE_SIZE // 2
    picture = Image.new("RGBA", (grid.width * TILE_SIZE, grid.height * TILE_SIZE), (0, 0, 0, 255))
    for z in range(4):
        for y in range(grid.height):
            for x in range(grid.width):
                tile_id = grid.get(x, y, z)
                if tile_id == 0:
                    continue
                px, py = x * TILE_SIZE, y * TILE_SIZE
                kind = autotile_kind(tile_id)
                if kind is not None:
                    slot = 0 if tile_id < TILE_ID_A2 else 1
                    kind = kind if slot == 0 else kind - 16
                    image = sheet(slot)
                    if image is None:
                        continue
                    shape = (tile_id - TILE_ID_A1) % 48
                    bx, by = (kind % 8) * 2 * TILE_SIZE, (kind // 8) * 3 * TILE_SIZE
                    for (qx, qy), (dx, dy) in zip(FLOOR_AUTOTILE_TABLE[shape], [(0, 0), (1, 0), (0, 1), (1, 1)]):
                        piece = image.crop((bx + qx * half, by + qy * half, bx + qx * half + half, by + qy * half + half))
                        picture.alpha_composite(piece, (px + dx * half, py + dy * half))
                elif TILE_ID_A5 <= tile_id < TILE_ID_A1:
                    image = sheet(4)
                    index = tile_id - TILE_ID_A5
                    sx, sy = (index % 8) * TILE_SIZE, (index // 8) * TILE_SIZE
                    picture.alpha_composite(image.crop((sx, sy, sx + TILE_SIZE, sy + TILE_SIZE)), (px, py))
                elif 0 < tile_id < TILE_ID_A5:
                    image = sheet(5 + tile_id // 256)
                    if image is None:
                        continue
                    index = tile_id % 256
                    column = index % 8 + (8 if index >= 128 else 0)
                    row = (index % 128) // 8
                    sx, sy = column * TILE_SIZE, row * TILE_SIZE
                    if sx + TILE_SIZE <= image.width and sy + TILE_SIZE <= image.height:
                        picture.alpha_composite(image.crop((sx, sy, sx + TILE_SIZE, sy + TILE_SIZE)), (px, py))
    if scale != 1.0:
        picture = picture.resize((int(picture.width * scale), int(picture.height * scale)), Image.LANCZOS)
    return picture


def load_map(path):
    """A map file as a MapGrid, with the file's other fields kept beside it."""
    with open(path, encoding="utf-8") as f:
        data = json.load(f)
    grid = MapGrid(data["width"], data["height"])
    grid.data = list(data["data"])
    return grid, data
