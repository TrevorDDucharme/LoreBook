# LoreBook — The Living Lore Platform

**LoreBook is not a note-taking app. It's a reality engine for worldbuilders.**

Build entire universes—from the cosmic scale of solar systems down to the trinket on a character's desk—and make every single piece *explorable, interactive, and alive*.

![C++23](https://img.shields.io/badge/C%2B%2B-23-blue.svg)
![Platform](https://img.shields.io/badge/platform-Linux-lightgrey.svg)
![License](https://img.shields.io/badge/license-MIT-green.svg)

---

## Why LoreBook?

Traditional worldbuilding tools give you folders and documents. LoreBook gives you **worlds**.

- Click on a continent to zoom into its nations
- Click on a city to explore its streets
- Click on a building to walk through its floor plan
- Click on a bookshelf to read the books on it
- Click on a character to see their full 3D model, then watch them walk across your map

Every layer of your world—from realities to solar systems to planets to countries to cities to buildings to rooms to people to individual objects—can have its own lore, its own visuals, its own interactivity.

**This is worldbuilding for creators who refuse to be limited by static documents.**

---

## Core Philosophy

### Everything Is a Node. Everything Is Connected.

In LoreBook, every piece of your world is a **node** in an interconnected web:

- A **planet** is a node containing continents
- A **city** is a node containing districts, buildings, and people
- A **character** is a node with biography, 3D model, inventory, and relationships
- A **spell book** is a node you can open and flip through
- A **sword** is a node with history, stats, and a 3D preview

Nodes can have **multiple parents**—a character can belong to a faction, live in a city, AND be part of a quest line simultaneously. Filter by any path to see different perspectives on your world.

### Not Just Notes—Experiences

LoreBook transforms passive lore into active exploration:

| Traditional Tools | LoreBook |
|-------------------|----------|
| A document describing a library | A 3D library you can walk through and pull books from shelves |
| A list of spells | An interactive spell book with animated pages |
| Character backstory in a text file | A living character with a 3D model that walks your world |
| A map image with annotations | A terrain you can sculpt, with clickable locations that open their lore |

---

## Features In Depth

### 🌍 Explorable World Maps

Your world isn't a static image—it's a geologically, ecologically, and climatically simulated reality.

**Realistic World Simulation**

LoreBook generates worlds using actual geological and ecological processes:

- **Tectonic Plate Simulation** — Define plate boundaries and watch continents form, mountains rise, and rift valleys open
- **Erosion Modeling** — Rivers carve canyons, rain shapes coastlines, glaciers sculpt valleys over simulated millennia
- **Rainfall & Humidity** — Water sources, prevailing winds, and mountain rain shadows create realistic climate zones
- **Ecology Generation** — Biomes emerge naturally from temperature, rainfall, and altitude: rainforests where it's wet, deserts in rain shadows, tundra at the poles

You control the simulation parameters—or override any region by hand.

**Layered World System**

Your world is built in layers, each editable independently:

| Layer | Contents |
|-------|----------|
| **Geological** | Terrain height, rock types, plate boundaries, fault lines |
| **Hydrological** | Rivers, lakes, aquifers, ocean currents, rainfall patterns |
| **Ecological** | Biomes, vegetation, wildlife distribution, resource deposits |
| **Political** | Nations, borders, territories, disputed regions, historical boundaries |
| **Infrastructure** | Roads, trade routes, cities, fortifications |
| **Magical/Custom** | Ley lines, mana wells, corruption zones, or any custom overlay |

Toggle layers on/off. Edit one without affecting others. Create your own custom layer types.

**Procedural City Generation**

Cities don't just appear—they emerge where civilizations would actually build:

- **Resource-Driven Placement** — Settlements spawn near water, fertile land, mineral deposits, and trade routes
- **Population Simulation** — Cities grow based on available resources and trade connections
- **Procedural Street Layouts** — Generate realistic city maps with districts, main roads, and alleyways
- **Building Population** — Fill cities with procedurally generated buildings using your prefab templates
- **Guided Generation** — Define districts (market, residential, noble quarter) and let the system fill them appropriately

You create the prefabs and guidelines; LoreBook populates your world.

**Hand-Crafted + Procedural Hybrid**

- Define regions where procedural generation applies
- Hand-place important locations and let the system fill around them
- Lock any element to prevent regeneration
- Paint "alteration zones" to guide procedural output (e.g., "this area is war-torn")

**Interactive Locations**
- Click any location to instantly open its lore node
- Nest maps within maps: world → continent → region → city → district → building → room
- Every generated element can be customized and given its own lore

**Dynamic Elements**
- Roads, rivers, and borders connect locations logically
- Place scriptable objects directly on the map
- Characters with full IK walk the terrain in real-time
- Weather systems, day/night cycles, seasonal changes

**Seamless Zoom**
- Transition from orbital view to street level without loading screens
- Each zoom level reveals appropriate detail
- Configure visibility per layer at each scale

---

### 🏛️ Floor Plans & Building Interiors

Design every interior space in your world—from sprawling castles to cramped tavern rooms.

**Room-Based Design**
- Draw walls, doors, windows, stairs
- Define room boundaries and properties
- Multi-floor buildings with connected levels

**Furniture & Objects**
- Place furniture, decorations, and interactive objects
- Each object can link to its own lore node
- Click a bookshelf to see what books are on it
- Click a chest to view its contents

**Explorable Spaces**
- Navigate your floor plans in first or third person
- Characters can walk through buildings with proper pathfinding
- Perfect for dungeons, mansions, shops, temples

**Templates & Prefabs**
- Save building designs as reusable templates
- Build a tavern once, place it in every city
- Customize instances while preserving the base design

---

### 📜 Scriptable Objects & Interactivity

Make your lore *do things*.

**What Are Scriptable Objects?**

Scriptable objects are lore nodes with behavior. They can:
- Display custom UI when clicked
- Change state based on conditions
- Trigger events in other parts of your world
- Store variables and track progress

**Examples**

| Object | Behavior |
|--------|----------|
| **Spell Book** | Opens to show pages you can flip through, with animated spell effects |
| **Locked Door** | Requires a specific key item; remembers if unlocked |
| **Quest Board** | Displays available quests; marks completed ones |
| **Magic Mirror** | Shows different content based on who's viewing it |
| **Day/Night Cycle** | Changes map lighting and NPC locations |

**Lua Scripting**
- Full Lua integration for custom behavior
- Access to vault data, node properties, and UI
- Event hooks for click, hover, proximity, time

---

### 🖼️ Rich Media & Custom Markdown Views

Your lore isn't just text—it's multimedia, interactive, and alive. LoreBook extends Markdown with powerful embeddable components.

**Embeddable 3D Model Viewer**
- Rotate, zoom, and inspect 3D models inline in your notes
- Support for glTF, GLB, FBX, OBJ formats
- Configure lighting, background, and default camera angle
- Perfect for weapons, artifacts, architecture, characters
- Models can be animated—show a sword being drawn or a door opening

```markdown
![My Artifact](vault://Assets/ancient_sword.glb){view3d autorotate}
```

**Inline Image Viewer**
- Embed images with automatic sizing and lightbox expansion
- Galleries for multiple images with navigation
- Support for PNG, JPEG, WebP, GIF (animated), SVG

**Music & Audio Player**
- Embed background music, ambient soundscapes, or voice recordings
- Playlists for location-specific atmosphere
- Loop, autoplay, and volume controls
- Perfect for tavern music, forest ambiance, character themes

```markdown
![Tavern Ambiance](vault://Assets/tavern_music.mp3){audio loop autoplay volume=0.3}
```

**Video Player**
- Embed video content directly in notes
- Support for MP4, WebM, and common formats
- Use for animated lore, cinematics, or tutorials

```markdown
![The Fall of Valdris](vault://Assets/valdris_cinematic.mp4){video}
```

**Scripted Canvas**
- Embed programmable 2D canvases in your notes
- Draw custom visualizations, animated diagrams, or mini-games
- Lua-scripted with full drawing API
- Perfect for magic circles that animate, star charts that rotate, or interactive puzzles

```markdown
![Magic Circle](vault://Scripts/magic_circle.lua){canvas width=400 height=400}
```

**Scripted UI Panels**
- Embed fully custom UI components in your notes
- Create character sheets, inventory displays, stat blocks
- Interactive forms that write back to vault data
- Build your own mini-applications within your lore

```markdown
![Character Sheet](vault://Scripts/character_sheet.lua){ui}
```

**Interactive Maps (Inline)**
- Embed world maps, dungeon maps, or floor plans directly in notes
- Clickable regions link to other nodes
- Mini-maps for location-based lore

**Wiki-Style Linking**
- Link to other nodes with `[[Node Name]]` syntax
- Hover preview shows node summary
- Backlinks automatically tracked

**Asset Management**
- Upload assets once, reference everywhere
- Automatic deduplication and organization
- Assets stored directly in your vault database
- Version tracking for updated assets

---

### 🧬 Multi-Parent Hierarchy & Filtering

Real worlds don't fit in simple folder trees. Neither should your lore.

**Multi-Parent Nodes**

A single node can exist in multiple places simultaneously:
- A **character** belongs to a faction AND lives in a city AND is involved in a quest
- A **weapon** is in a character's inventory AND displayed in a museum AND referenced in a prophecy
- A **building** is in a city AND part of a historical event AND owned by an organization

**Dynamic Filtering**

View your world through different lenses:
- Filter by **tags**: Show only "magic items" or "antagonists"
- Filter by **parent path**: See everything under "Kingdom of Valdris"
- Combine filters with AND/OR logic
- Save filter presets for quick access

**Per-Node Child Filters**

Configure how each node displays its children:
- A "Characters" node shows only nodes tagged "character"
- A "Timeline" node sorts children chronologically
- A "Faction" node groups members by rank

**Graph Visualization**

See your entire world as an interactive node graph:
- Visualize connections between nodes
- Drag to rearrange, zoom to explore
- Color-code by type, tag, or custom criteria

---

### 👤 Full Character Creator

Build detailed, animated characters that inhabit your world.

**Socket-Centric Architecture**

Characters are assembled from modular parts connected by sockets:
- Mix and match heads, bodies, arms, legs, tails, wings
- Each part carries its own mesh, rig, and deformation data
- Parts automatically align via socket compatibility

**Part Library**

Build a library of reusable character components:
- Import parts from glTF, GLB, FBX, OBJ files
- Categorize by type: heads, torsos, limbs, accessories
- Tag parts for easy filtering: "human", "elf", "armor", "fantasy"

**Custom Asset Upload**

Bring your own models into the system:
- Import any compatible 3D format
- Automatic socket detection from bone naming
- Define custom socket profiles for non-standard parts

**Shape Keys & Morphs**

Customize character appearance:
- Blend between shape key targets
- Create facial expressions, body variations
- Socket-driven shape keys activate on attachment

**Characters In Your World**

Once created, characters aren't static:
- Display character models inline in their lore notes
- Place characters on world maps as interactive markers
- Watch them walk across terrain with full IK locomotion
- Characters respect floor plans and navigate interiors

---

### 🦿 Inverse Kinematics & Procedural Animation

Characters move naturally through your world without pre-baked animations.

**Part-Role-Aware IK**

The IK system understands anatomy:
- Legs plant on terrain with proper foot placement
- Arms reach for targets with natural elbow positioning
- Spines curve, heads track, tails sway
- Wings fold and extend based on context

**Procedural Locomotion**

Characters walk, run, and navigate dynamically:
- Gait adapts to movement speed
- Feet adjust to slopes and stairs
- Bodies lean into turns
- No pre-made walk cycles required

**Surface-Conforming Digits**

Fingers and toes wrap around objects:
- Grip surfaces naturally when grabbing objects
- Toes conform to terrain during locomotion
- Configurable curl, spread, and grip strength

**Look-At System**

Characters track points of interest:
- Eyes, head, and upper body follow targets
- Configurable limits and blend weights
- Chain multiple body parts for natural motion

---

### 🤖 AI-Powered Worldbuilding Assistant

An AI that knows your world as well as you do.

**Context-Aware Responses**

The assistant doesn't just answer questions—it answers questions *about your world*:
- Ask "Who rules the northern kingdoms?" and get answers from YOUR lore
- Ask "What happened at the Battle of Ironhold?" referencing YOUR history
- Generate new content that fits YOUR established canon

**Retrieval-Augmented Generation (RAG)**

The assistant automatically retrieves relevant nodes:
- Full-text search finds related lore
- Keyword extraction identifies important concepts
- Context is injected into every query

**Structured Output**

Generate lore in structured formats:
- Create new nodes directly from AI responses
- Generate character sheets, location details, item stats
- Output validated JSON for consistent data

**Flexible Endpoints**

Use any OpenAI-compatible API:
- OpenAI, Azure OpenAI, Anthropic
- Local servers: LMStudio, Ollama, text-generation-webui
- Custom endpoints with configurable headers

---

### 📚 The Vision: Living Libraries & Interactive Spell Books

LoreBook enables experiences impossible in traditional tools.

**The Explorable Library**

Imagine a library in your world:
1. Click the library on your city map
2. Enter the floor plan—shelves, tables, reading nooks
3. Click a bookshelf to see its contents
4. Click a book to open it—pages you can flip
5. Each book links to its lore node with full text

**The Interactive Spell Book**

A wizard's tome isn't a list—it's an artifact:
1. Open the spell book as a scriptable object
2. Animated pages turn as you browse
3. Each spell shows incantation, effects, history
4. Click to see 3D visualization of the spell effect
5. Track which spells the character has mastered

**The Living Museum**

A museum of artifacts in your world:
1. Walk the floor plan with your character
2. Approach display cases to examine items
3. Each artifact is a node with full history
4. Embedded 3D model lets you rotate and inspect
5. Placards link to related lore

---

### 🔭 Scales of Reality

Build at any scale—and connect them all.

| Scale | Examples | Features |
|-------|----------|----------|
| **Cosmic** | Universes, dimensions, planes | Abstract maps, connection rules between realities |
| **Stellar** | Solar systems, star clusters | Orbital mechanics, planet placement |
| **Planetary** | Worlds, moons, continents | Terrain sculpting, biome painting, climate zones |
| **Regional** | Nations, territories, wilderness | Political boundaries, travel routes, landmarks |
| **Urban** | Cities, towns, villages | District layouts, notable buildings, populations |
| **Architectural** | Buildings, dungeons, ships | Floor plans, room contents, interior navigation |
| **Personal** | Characters, creatures, NPCs | 3D models, inventories, relationships, AI behavior |
| **Detailed** | Items, artifacts, trinkets | 3D previews, history, properties, current location |

Each scale links to the others. Zoom from galaxy to grain of sand, with lore at every level.

---

## Getting Started

### Quick Start

1. **Launch LoreBook**
2. **Create a vault**: `File → New Vault` — this is your world's database
3. **Create your first location**: Right-click in the Vault Tree → "New Child"
4. **Explore**: Open the World Map, Graph View, or Floor Plan Editor from the View menu
5. **Build characters**: Open the Character Editor and start assembling

### Your First World

**Step 1: Create the Structure**

Start broad and drill down:
```
My World (root node)
├── Geography
│   ├── Northern Continent
│   │   ├── Kingdom of Valdris
│   │   │   ├── Capital City
│   │   │   │   ├── Royal Palace (floor plan)
│   │   │   │   ├── Market District
│   │   │   │   └── Temple of the Sun
│   │   │   └── Coastal Towns
│   │   └── The Frozen Wastes
│   └── Southern Isles
├── Factions
│   ├── The Crown
│   ├── Merchant Guilds
│   └── The Shadow Court
├── Characters
│   ├── Protagonists
│   └── Antagonists
└── Items & Artifacts
    ├── Weapons
    └── Magic Items
```

**Step 2: Add Rich Content**

For each node:
- Write lore in Markdown with the content editor
- Embed images: `![](vault://Assets/my-image.png)`
- Embed 3D models: `![](vault://Assets/artifact.glb)`
- Link to other nodes: `[[Kingdom of Valdris]]`
- Add tags for filtering: `location`, `capital`, `magic`

**Step 3: Build the World Map**

- Open `View → World Map`
- Sculpt terrain: mountains, rivers, forests
- Place location markers linked to your nodes
- Define regions and borders

**Step 4: Design Interiors**

- Open `View → Floor Plan Editor`
- Draw the layout of important buildings
- Place furniture and interactive objects
- Link rooms to their lore nodes

**Step 5: Create Characters**

- Open `View → Character Editor`
- Import or select body parts from the library
- Assemble via socket connections
- Save to your vault and embed in notes

---

## Technical Details

### Building From Source

**Prerequisites**
- Linux (primary platform)
- GCC 13+ or Clang 16+ with C++23 support
- CMake 3.10+
- vcpkg package manager

**Build Steps**

```bash
# Clone
git clone https://github.com/yourusername/LoreBook.git
cd LoreBook

# Configure (adjust vcpkg path)
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake

# Build
cmake --build build --target LoreBook -j$(nproc)

# Run
./bin/LoreBook
```

### Technology Stack

| Component | Technology | Purpose |
|-----------|------------|---------|
| UI Framework | Dear ImGui | Immediate-mode GUI with docking |
| Graphics | OpenGL 3.3+ | 3D rendering, model viewer |
| Database | SQLite / MySQL | Local and remote vault storage |
| 3D Import | Assimp | glTF, FBX, OBJ model loading |
| Networking | libcurl | API calls, asset fetching |
| Scripting | Lua | Scriptable object behavior |
| AI/LLM | OpenAI API | Worldbuilding assistant |
| Markdown | md4c | Content parsing and rendering |

### Storage Architecture

**Vaults** are self-contained databases storing:
- All nodes and their relationships (multi-parent)
- Full text content with version history
- Embedded assets (images, models, audio)
- User accounts and permissions
- Floor plan templates and character prefabs

**Local vaults** use SQLite—single file, portable, no server needed.

**Remote vaults** use MySQL—collaborative editing, cloud backup, team access.

---

## For Developers

### Project Structure

```
LoreBook/
├── src/
│   ├── LoreBook.cpp              # Application entry, main loop
│   ├── Vault.cpp                 # Core data model, DB operations
│   ├── VaultAssistant.cpp        # RAG retrieval, AI integration
│   ├── VaultChat.cpp             # Chat UI, streaming
│   ├── GraphView.cpp             # Node graph visualization
│   ├── WorldMaps/                # Terrain, map rendering
│   │   ├── WorldMap.cpp
│   │   └── Buildings/
│   │       └── FloorPlanEditor.cpp
│   └── CharacterEditor/          # Character composition system
│       ├── CharacterEditorUI.cpp
│       ├── PartLibrary.cpp
│       ├── ModelLoader.cpp
│       └── IKSystem.cpp
├── include/                      # Headers
├── docs/                         # Design documents
│   └── CharacterEditor/
│       └── SocketCentricArchitecture.md
├── LoreBook_Resources/           # Embedded fonts, icons
├── CMakeLists.txt
└── vcpkg.json
```

### Contributing

LoreBook follows a **run-first development** workflow:

1. Make changes
2. Build and run the application
3. Test manually—verify the feature works as intended
4. Debug with GDB/LLDB as needed
5. Submit PR with verification steps

We prioritize working software over test coverage. Include screenshots or recordings for UI changes.

---

## The Living Lore Vision

LoreBook exists because worldbuilders deserve better tools.

We're not building another wiki. We're not building another note app with a graph view tacked on. We're building the tool we wished existed—one where worlds feel *real*, where lore is *explorable*, where characters *live*.

**If you've ever wanted to:**
- Walk through the castle you've been writing about for years
- Click on that artifact in your story and see it from every angle
- Have an AI that actually knows who King Aldric is and why he matters
- Build a magic system your readers can interact with, not just read about
- Create a world where every detail connects to every other detail

**Then LoreBook is for you.**

---

## License

MIT License. See [LICENSE](LICENSE) for details.

---

## Acknowledgments

Built with:
- [Dear ImGui](https://github.com/ocornut/imgui) — The magic behind the UI
- [Assimp](https://github.com/assimp/assimp) — Bringing 3D models to life
- [SQLite](https://sqlite.org/) — Rock-solid data storage
- [vcpkg](https://vcpkg.io/) — Dependency sanity

---

## Join the Journey

LoreBook is under active development. Star the repo, open issues, submit PRs—let's build the ultimate worldbuilding tool together.

**Your worlds deserve to be more than documents. Make them real.**
