"""Makes Nalluna Plains, the open field south of the town, as a normal RPG Maker map in the town's Time Fantasy tiles.

    python make_plains.py [--seed N] [--size 150] [--preview picture.png]

It writes game/data/Map002.json, the map's line in MapInfos.json and its tileset (the town's, with grass-shored
water) in Tilesets.json, and puts the road between the town's south exit and the plains' north edge on both maps. The same seed always gives the same plains; the map can be opened and
changed in RPG Maker afterwards like any other (running this again replaces it).

The plains: meadows of grass with flowers, bushes and rocks; woods that thicken toward the edges, so the map's border
is a wall of trees; lakes with reeds on their shores; and dirt roads from the town's gate to a few places worth
walking to, each in a clearing. Regions (the editor's R layer) mark the kinds of ground, and each kind has its own
monsters (ENCOUNTERS): 1 meadow, 2 woods, 3 lakeside, 4 the road near the gate, where nothing attacks."""
import argparse
import heapq
import json
import os
import random

import numpy as np
from PIL import Image

import rmmz_map as rm

HERE = os.path.dirname(os.path.abspath(__file__))
GAME = os.path.join(HERE, "..", "game")
TOWN_MAP_ID = 1
PLAINS_MAP_ID = 2
PLAINS_NAME = "Nalluna Plains"
PLAINS_TILESET_ID = 7  # the town's Time Fantasy tileset with the outdoor water sheet, whose shores are grass
PLAINS_TILESET_NAME = "Time Fantasy Plains"

# Ground, from the town's Time Fantasy tileset ("Time Fantasy Town", tileset 1).
GRASS = rm.a2_tile(0)
ROAD = rm.a2_tile(1)  # dirt with a grass border, the town's roads
WATER = rm.a1_tile(0)
GRASS_ON_A5 = {"flowers": [16, 17, 18, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 44, 45, 46, 47],
               "tall grass": [40, 41, 42, 43], "reeds": [36, 37, 38, 39]}
# Small things on the grass, from the B sheet: bushes and rocks; each blocks the way.
BUSHES = [72, 73, 74, 75]
ROCKS = [60, 61, 62]

# The trees of the E sheet (bonus_trees): four shapes in three colours, each a band of four rows. Shapes A and B are
# three tiles wide, C and D two; a forest is made of C and D, which stack neatly.
TREE_COLUMNS = {"A": [0, 1, 2], "B": [3, 4, 5], "C": [6, 7], "D": [8, 9]}
TREE_ROWS = 4
COLOURS = {"green": 0, "spring": 1, "teal": 2}  # band of rows on the sheet

# The monsters met while walking, by region (troop ids from Troops.json): goblins and crows on the meadows, gnomes and
# treants in the woods, crows and gnomes by the lakes; none on the road near the gate.
ENCOUNTERS = [
    {"troopId": 1, "weight": 10, "regionSet": [1]},  # Goblin*2
    {"troopId": 3, "weight": 6, "regionSet": [1, 3]},  # Crow*2
    {"troopId": 2, "weight": 8, "regionSet": [2, 3]},  # Gnome*2
    {"troopId": 4, "weight": 3, "regionSet": [2]},  # Treant
]
ENCOUNTER_STEPS = 30  # steps between fights, on average
BATTLEBACK = "Grassland"


def e_index(column, row):
    """The E sheet's own number for the tile at this column and row (left half first, as the editor counts)."""
    return row * 8 + column if column < 8 else 128 + row * 8 + (column - 8)


def tree_stamps():
    """Every tree as a stamp: its shape, colour, and the (dx, dy, tile id) of each tile that has something drawn."""
    sheet = Image.open(os.path.join(GAME, "img", "tilesets", "bonus_trees.png")).convert("RGBA")
    alpha = np.array(sheet)[:, :, 3]
    stamps = []
    for colour, band in COLOURS.items():
        for shape, columns in TREE_COLUMNS.items():
            tiles = []
            for dy in range(TREE_ROWS):
                for dx, column in enumerate(columns):
                    row = band * TREE_ROWS + dy
                    block = alpha[row * 48:(row + 1) * 48, column * 48:(column + 1) * 48]
                    if (block > 0).sum() > 40:
                        tiles.append((dx, dy, rm.e_tile(e_index(column, row))))
            stamps.append({"shape": shape, "colour": colour, "width": len(columns), "tiles": tiles})
    return stamps


def smooth_noise(width, height, cell, rng, octaves=3):
    """Values from 0 to 1 that change smoothly over about `cell` tiles, with finer detail on top."""
    total = np.zeros((height, width))
    weight = 0.0
    for octave in range(octaves):
        size = max(2, cell // (2 ** octave))
        grid = rng.random((height // size + 2, width // size + 2))
        ys = np.arange(height) / size
        xs = np.arange(width) / size
        y0, x0 = ys.astype(int), xs.astype(int)
        fy, fx = (ys - y0)[:, None], (xs - x0)[None, :]
        fy, fx = fy * fy * (3 - 2 * fy), fx * fx * (3 - 2 * fx)
        a = grid[y0][:, x0]
        b = grid[y0][:, x0 + 1]
        c = grid[y0 + 1][:, x0]
        d = grid[y0 + 1][:, x0 + 1]
        layer = (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy
        amplitude = 0.5 ** octave
        total += layer * amplitude
        weight += amplitude
    return total / weight


class Plains:
    """The plains as they are laid out, before they become tiles."""

    def __init__(self, size, seed):
        self.size = size
        self.rng = random.Random(seed)
        self.np_rng = np.random.default_rng(seed)
        self.water = np.zeros((size, size), bool)
        self.road = np.zeros((size, size), bool)
        self.clear = np.zeros((size, size), bool)  # no trees here: roads, clearings, the gate
        self.gate = (size // 2 - 1, size // 2, size // 2 + 1)  # the three road tiles on the north edge
        self.places = {}  # name -> (x, y) of the places the roads lead to
        edge = np.minimum.reduce(np.meshgrid(np.arange(size), np.arange(size)) +
                                 np.meshgrid(size - 1 - np.arange(size), size - 1 - np.arange(size)))
        self.edge_distance = edge
        woods = smooth_noise(size, size, 34, self.np_rng)
        woods = (woods - woods.min()) / (woods.max() - woods.min())  # 0 to 1 over the whole map
        self.woodland = woods * 0.95 + np.clip(1 - edge / 9, 0, 1) * 0.9
        self.thick = np.quantile(self.woodland, 0.62)  # packed forest above this
        self.thin = np.quantile(self.woodland, 0.42)  # scattered trees between this and thick
        self.colour = smooth_noise(size, size, 24, self.np_rng)
        self.flowers = smooth_noise(size, size, 12, self.np_rng)
        self.tall_grass = smooth_noise(size, size, 9, self.np_rng)

    def in_bounds(self, x, y):
        return 0 <= x < self.size and 0 <= y < self.size

    def lay_lakes(self):
        """Two or three lakes in open ground, each a blob with a ragged shore."""
        s = self.size
        spots = [(int(s * 0.22), int(s * 0.5), 9), (int(s * 0.74), int(s * 0.66), 7), (int(s * 0.58), int(s * 0.22), 5)]
        shore = smooth_noise(s, s, 6, self.np_rng)
        ys, xs = np.mgrid[0:s, 0:s]
        for cx, cy, radius in spots:
            distance = np.hypot(xs - cx, (ys - cy) * 1.2)
            self.water |= distance < radius * (0.75 + 0.5 * shore)
            self.places["lake %d" % len(self.places)] = (cx, cy - radius - 3)

    def choose_places(self):
        """Where the roads go: a hub in the middle, a camp, a ruin, the far side, the woods' cave mouth."""
        s = self.size
        self.places.update({
            "gate": (self.gate[1], 1),
            "crossroads": (s // 2, int(s * 0.42)),
            "camp": (int(s * 0.80), int(s * 0.34)),
            "old stones": (int(s * 0.30), int(s * 0.80)),
            "far road": (s // 2 + 8, s - 2),
            "cave mouth": (int(s * 0.12), int(s * 0.18)),
            "east meadow": (int(s * 0.86), int(s * 0.86)),
        })

    def road_cost(self):
        """How hard each tile is to build a road through: open grass is easy, woods are hard, water is impossible."""
        wiggle = smooth_noise(self.size, self.size, 7, self.np_rng)
        cost = 1 + np.clip(self.woodland - self.thin, 0, None) * 40 + wiggle * 6
        cost[self.water] = np.inf
        return cost

    def route(self, start, goal, cost):
        """The cheapest path between two tiles (A*), as a list of tiles."""
        s = self.size
        best = {start: 0.0}
        came = {}
        frontier = [(0.0, start)]
        while frontier:
            _, here = heapq.heappop(frontier)
            if here == goal:
                break
            x, y = here
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nx, ny = x + dx, y + dy
                if not (1 <= nx < s - 1 and 0 <= ny < s) or not np.isfinite(cost[ny, nx]):
                    continue
                step = best[here] + cost[ny, nx] + (0.2 if self.road[ny, nx] else 0)
                if self.road[ny, nx]:
                    step -= 0.6  # join roads that are already there
                if step < best.get((nx, ny), np.inf):
                    best[(nx, ny)] = step
                    came[(nx, ny)] = here
                    guess = abs(goal[0] - nx) + abs(goal[1] - ny)
                    heapq.heappush(frontier, (step + guess, (nx, ny)))
        path = [goal]
        while path[-1] != start:
            path.append(came[path[-1]])
        return path[::-1]

    def lay_roads(self):
        """Roads from the gate to the crossroads, and from there to every other place; each two tiles wide."""
        cost = self.road_cost()
        links = [("gate", "crossroads"), ("crossroads", "camp"), ("crossroads", "old stones"),
                 ("old stones", "far road"), ("crossroads", "east meadow"), ("crossroads", "cave mouth")]
        links += [("crossroads", name) for name in self.places if name.startswith("lake")]
        for a, b in links:
            for x, y in self.route(self.places[a], self.places[b], cost):
                for wx, wy in ((x, y), (x + 1, y), (x, y + 1), (x + 1, y + 1)):
                    if self.in_bounds(wx, wy) and not self.water[wy, wx]:
                        self.road[wy, wx] = True
        for x in self.gate:
            for y in range(0, 4):
                self.road[y, x] = True
        self.close_road_gaps()

    def close_road_gaps(self):
        """Makes roads that run side by side one wide road: grass with road on both sides becomes road."""
        for _ in range(2):
            road = self.road
            across = np.roll(road, 1, 1) & np.roll(road, -1, 1)
            down = np.roll(road, 1, 0) & np.roll(road, -1, 0)
            self.road = road | ((across | down) & ~self.water)

    def clear_ground(self):
        """No trees on the roads and a tile either side, in a clearing round every place, or in the water."""
        s = self.size
        ys, xs = np.mgrid[0:s, 0:s]
        grown = self.road.copy()
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (-1, -1), (1, -1), (-1, 1)):
            grown |= np.roll(np.roll(self.road, dy, 0), dx, 1)
        self.clear |= grown | self.water
        for name, (px, py) in self.places.items():
            radius = 4 if name.startswith("lake") or name in ("gate", "far road") else 7
            self.clear |= np.hypot(xs - px, ys - py) < radius


def plant_trees(plains, stamps):
    """Where every tree stands: a packed forest where the woodland is thick, single trees dotted where it is thin."""
    rng, s = plains.rng, plains.size
    by_shape = {}
    for stamp in stamps:
        by_shape.setdefault(stamp["shape"], []).append(stamp)
    taken = np.zeros((s, s), bool)  # trunks and crowns of trees already planted, for spacing single trees

    def colour_at(x, y):
        value = plains.colour[min(y, s - 1), min(x, s - 1)]
        return "teal" if value < 0.36 else "spring" if value > 0.62 else "green"

    def fits(stamp, x, y, past_edge=False):
        """Whether the tree can stand here: on the map (or partly past its edge, if allowed), clear of roads,
        clearings and water."""
        for dx, dy, _ in stamp["tiles"]:
            tx, ty = x + dx, y + dy
            if not plains.in_bounds(tx, ty):
                if past_edge:
                    continue
                return False
            if plains.clear[ty, tx]:
                return False
        return True

    trees = []
    # The packed forest: two-wide trees every two tiles across and every two tiles down, every other row shifted.
    for row, y in enumerate(range(-2, s, 2)):
        for x in range(-1 + (row % 2), s, 2):
            cx, cy = min(max(x + 1, 0), s - 1), min(max(y + 3, 0), s - 1)
            if plains.woodland[cy, cx] < plains.thick or rng.random() < 0.06:
                continue
            stamp = rng.choice([t for t in by_shape["C"] + by_shape["D"] if t["colour"] == colour_at(cx, cy)])
            if fits(stamp, x, y, past_edge=True):
                trees.append((x, y, stamp))
                for dx, dy, _ in stamp["tiles"]:
                    if plains.in_bounds(x + dx, y + dy):
                        taken[y + dy, x + dx] = True
    # Single trees in the thinner woodland and now and then out on the meadow.
    for _ in range(s * s // 9):
        x, y = rng.randrange(s), rng.randrange(s)
        density = plains.woodland[y, x]
        if density >= plains.thick or rng.random() > (0.45 if density > plains.thin else 0.09):
            continue
        stamp = rng.choice([t for t in stamps if t["colour"] == colour_at(x, y)])
        margin = [(x + dx, y + dy) for dx in range(-1, stamp["width"] + 1) for dy in range(0, TREE_ROWS + 1)]
        if fits(stamp, x, y) and not any(plains.in_bounds(mx, my) and taken[my, mx] for mx, my in margin):
            trees.append((x, y, stamp))
            for dx, dy, _ in stamp["tiles"]:
                taken[y + dy, x + dx] = True
    return trees, taken


def lay_trees(grid, trees):
    """Puts the trees on layers 2 and 3: where two overlap, the one further down (in front) goes on the upper layer."""
    tiles = {}
    for x, y, stamp in sorted(trees, key=lambda tree: tree[1]):
        for dx, dy, tile_id in stamp["tiles"]:
            if grid.inside(x + dx, y + dy):
                tiles.setdefault((x + dx, y + dy), []).append(tile_id)
    for (x, y), stack in tiles.items():
        grid.set(x, y, 2, stack[-2] if len(stack) > 1 else stack[-1])
        if len(stack) > 1:
            grid.set(x, y, 3, stack[-1])


def lay_ground(grid, plains, taken):
    """Grass, roads and water on layer 0; flowers, tall grass and reeds on layer 1; bushes and rocks here and there on
    layer 2."""
    rng, s = plains.rng, plains.size
    near_water = plains.water.copy()
    for _ in range(2):
        near_water = near_water | np.roll(near_water, 1, 0) | np.roll(near_water, -1, 0) | np.roll(near_water, 1, 1) \
            | np.roll(near_water, -1, 1)
    for y in range(s):
        for x in range(s):
            if plains.water[y, x]:
                grid.set(x, y, 0, WATER)
                continue
            grid.set(x, y, 0, ROAD if plains.road[y, x] else GRASS)
            if plains.road[y, x] or taken[y, x]:
                continue
            roll = rng.random()
            if near_water[y, x] and roll < 0.30:
                grid.set(x, y, 1, rm.TILE_ID_A5 + rng.choice(GRASS_ON_A5["reeds"]))
            elif plains.flowers[y, x] > 0.62 and roll < 0.45:
                grid.set(x, y, 1, rm.TILE_ID_A5 + rng.choice(GRASS_ON_A5["flowers"]))
            elif plains.tall_grass[y, x] > 0.6 and roll < 0.5:
                grid.set(x, y, 1, rm.TILE_ID_A5 + rng.choice(GRASS_ON_A5["tall grass"]))
            elif roll < 0.03:
                grid.set(x, y, 1, rm.TILE_ID_A5 + rng.choice(GRASS_ON_A5["flowers"]))
            elif roll < 0.042 and not plains.clear[y, x]:
                grid.set(x, y, 2, rng.choice(BUSHES))
            elif roll < 0.047 and not plains.clear[y, x]:
                grid.set(x, y, 2, rng.choice(ROCKS))
    grid.join_autotiles(0)


def lay_regions(grid, plains):
    """Region ids on the R layer: 1 meadow, 2 woods, 3 lakeside, 4 the road near the gate."""
    s = plains.size
    for y in range(s):
        for x in range(s):
            if plains.water[y, x]:
                continue
            gx, gy = plains.places["gate"]
            if abs(x - gx) + y < 14:
                region = 4
            elif plains.woodland[y, x] > plains.thin:
                region = 2
            else:
                region = 1
            for dx in range(-3, 4):
                for dy in range(-3, 4):
                    if plains.in_bounds(x + dx, y + dy) and plains.water[y + dy, x + dx]:
                        region = 3 if region != 4 else region
            grid.set(x, y, 5, region)


def transfer_event(event_id, name, x, y, to_map, to_x, to_y, direction):
    """An event on the map's edge that moves the player to another map when they step on it."""
    return {
        "id": event_id, "name": name, "note": "", "x": x, "y": y,
        "pages": [{
            "conditions": {"actorId": 1, "actorValid": False, "itemId": 1, "itemValid": False, "selfSwitchCh": "A",
                           "selfSwitchValid": False, "switch1Id": 1, "switch1Valid": False, "switch2Id": 1,
                           "switch2Valid": False, "variableId": 1, "variableValid": False, "variableValue": 0},
            "directionFix": False,
            "image": {"characterIndex": 0, "characterName": "", "direction": 2, "pattern": 0, "tileId": 0},
            "list": [{"code": 201, "indent": 0, "parameters": [0, to_map, to_x, to_y, direction, 0]},
                     {"code": 0, "indent": 0, "parameters": []}],
            "moveFrequency": 3, "moveRoute": {"list": [{"code": 0, "parameters": []}], "repeat": True,
                                              "skippable": False, "wait": False},
            "moveSpeed": 3, "moveType": 0, "priorityType": 0, "stepAnime": False, "through": False, "trigger": 1,
            "walkAnime": True,
        }],
    }


def town_south_exit(town):
    """The town's road tiles on its bottom edge, left to right."""
    columns = [x for x in range(town["width"])
               if rm.autotile_kind(town["data"][(town["height"] - 1) * town["width"] + x]) == rm.autotile_kind(ROAD)]
    if not columns:
        raise SystemExit("The town has no road on its bottom edge to join the plains to.")
    return columns


def join_town(town, plains):
    """The town's south road leads to the plains' north edge, and back: transfer events on both, side by side."""
    town_columns = town_south_exit(town)
    town_events = [e for e in town["events"] if e and not e["name"].startswith("To " + PLAINS_NAME)]
    town["events"] = [None] + [dict(e, id=i + 1) for i, e in enumerate(town_events)]
    plains_events = [None]
    for column, gate_x in zip(town_columns, plains.gate):
        town["events"].append(transfer_event(len(town["events"]), "To " + PLAINS_NAME, column, town["height"] - 1,
                                             PLAINS_MAP_ID, gate_x, 1, 2))
        plains_events.append(transfer_event(len(plains_events), "To town", gate_x, 0, TOWN_MAP_ID, column,
                                            town["height"] - 2, 8))
    return plains_events


def write_json(path, data):
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        json.dump(data, f, ensure_ascii=False, separators=(",", ":"))


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--size", type=int, default=150)
    parser.add_argument("--preview", help="also draw the whole map into this picture")
    args = parser.parse_args()

    data_folder = os.path.join(GAME, "data")
    with open(os.path.join(data_folder, "Map%03d.json" % TOWN_MAP_ID), encoding="utf-8") as f:
        town = json.load(f)
    tilesets_path = os.path.join(data_folder, "Tilesets.json")
    tilesets = json.load(open(tilesets_path, encoding="utf-8"))
    while len(tilesets) <= PLAINS_TILESET_ID:
        tilesets.append(None)
    plains_tileset = dict(tilesets[town["tilesetId"]], id=PLAINS_TILESET_ID, name=PLAINS_TILESET_NAME)
    plains_tileset["tilesetNames"] = ["tileA1_outside"] + plains_tileset["tilesetNames"][1:]
    tilesets[PLAINS_TILESET_ID] = plains_tileset
    write_json(tilesets_path, tilesets)

    plains = Plains(args.size, args.seed)
    plains.lay_lakes()
    plains.choose_places()
    plains.lay_roads()
    plains.clear_ground()
    trees, taken = plant_trees(plains, tree_stamps())
    grid = rm.MapGrid(args.size, args.size)
    lay_ground(grid, plains, taken)
    lay_trees(grid, trees)
    lay_regions(grid, plains)

    plains_map = {key: value for key, value in town.items() if key not in ("data", "events")}
    plains_map.update({"displayName": PLAINS_NAME, "tilesetId": PLAINS_TILESET_ID, "width": args.size,
                       "height": args.size, "note": "", "encounterList": ENCOUNTERS,
                       "encounterStep": ENCOUNTER_STEPS, "specifyBattleback": True, "battleback1Name": BATTLEBACK,
                       "battleback2Name": BATTLEBACK,
                       "data": grid.data, "events": join_town(town, plains)})
    write_json(os.path.join(data_folder, "Map%03d.json" % PLAINS_MAP_ID), plains_map)
    write_json(os.path.join(data_folder, "Map%03d.json" % TOWN_MAP_ID), town)

    infos_path = os.path.join(data_folder, "MapInfos.json")
    infos = json.load(open(infos_path, encoding="utf-8"))
    while len(infos) <= PLAINS_MAP_ID:
        infos.append(None)
    infos[PLAINS_MAP_ID] = {"id": PLAINS_MAP_ID, "expanded": False, "name": PLAINS_NAME, "order": PLAINS_MAP_ID,
                            "parentId": 0, "scrollX": 0, "scrollY": 0}
    write_json(infos_path, infos)

    if args.preview:
        rm.render(grid, plains_tileset["tilesetNames"], os.path.join(GAME, "img", "tilesets"),
                  scale=0.25).save(args.preview)
    print("Nalluna Plains: %dx%d, %d trees, %d road tiles, %d water tiles, seed %d" % (
        args.size, args.size, len(trees), int(plains.road.sum()), int(plains.water.sum()), args.seed))


if __name__ == "__main__":
    main()
