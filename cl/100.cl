# 100.cl - every construct the cl grammar and evaluator currently support,
# gathered in one file that parses AND evaluates cleanly end to end.
#
# The vocabulary here (greenhouse zones, plants, care logs) is deliberately
# unrelated to Terraform/HCL conventions - no "var", "resource", "provider",
# "variable", "output", "locals", "region", "aws", "tags", and so on. Every
# name below is just this file's own vocabulary; nothing is a keyword to
# this library (see README.md).

# --- scalar attributes ---------------------------------------------------

title        = "Greenhouse Almanac"
capacity     = 42
temperature  = 21.5
is_open      = true
caretaker    = null
motto        = "Every leaf says \"grow\"."

# --- objects, including nested objects and quoted/hyphenated keys --------

settings = {
  brightness = 80
  climate = {
    humidity = 55
    airflow  = "medium"
  }
}

inventory = {
  "seed-trays" = 12
  pots         = 30
}

day_mode   = { humidity = 55, airflow = "low" }
night_mode = { humidity = 70, airflow = "high" }

# --- lists / tuples, including a list of objects ---------------------------

zones       = ["seedling", "bloom", "harvest"]
coordinates = [12, [3, 4], "corner"]

garden_zones = [
  { label = "seedling", humidity = 55 },
  { label = "bloom",    humidity = 65 },
  { label = "harvest",  humidity = 70 },
]

care_log = [
  { zone = "seedling", task = "water" },
  { zone = "bloom",    task = "water" },
  { zone = "seedling", task = "prune" },
]

# --- blocks: unlabeled and labeled (multiple instances of the same type) -

climate_control {
  target_humidity = 60
  fans_enabled     = true
}

plant "fern" {
  water_per_day = 2
  sunlight      = "indirect"
}

plant "cascade" {
  water_per_day = 1.5
  sunlight      = "indirect"
}

plant "moss" {
  water_per_day = 0.5
  sunlight      = "shade"
}

# A block with two labels (Terraform's "resource "type" "name" {}" shape)
# is addressed by consuming both leading labels, not just the first one.
wing "east" "upper" {
  climate = "warm"
}

wing "west" "lower" {
  climate = "cool"
}

# --- traversal: attribute root, block-label root, index, splat -----------
#
# Note: only a *labeled* block can be reached this way ("plant.fern...").
# An unlabeled block like "climate_control" above can't - the resolution
# algorithm needs the leading traversal steps to double as the block's
# labels, and an unlabeled block never has any. See README.md, "How
# traversal resolution works".

nested_humidity  = settings.climate.humidity
picked_setting   = settings["brightness"]
fern_water       = plant.fern.water_per_day
moss_sunlight    = plant.moss.sunlight
upper_climate    = wing.east.upper.climate
lower_climate    = wing.west.lower.climate
first_zone       = zones[0]
third_coordinate = coordinates[2]
zone_labels      = garden_zones.*.label
zone_humidities  = garden_zones[*].humidity

# --- operators: arithmetic, comparison, logical, unary --------------------

total_capacity   = capacity + 8
half_capacity    = capacity / 2
capacity_mod     = capacity % 5
is_roomy         = capacity >= 40 && is_open
is_not_open      = !is_open
negative_offset  = -temperature
no_caretaker     = caretaker == null
mixed_comparison = (capacity > 10) == (temperature < 30)

# --- conditional ------------------------------------------------------------

active_mode = is_open ? "welcoming visitors" : "closed for the day"

# --- for-expressions: list form, object form, key+val, if-filter, grouping -

zones_upper      = [for z in zones : upper(z)]
indexed_zones    = [for i, z in zones : "${i}:${z}"]
long_zone_names  = [for z in zones : z if length(z) > 5]
zone_lengths     = {for z in zones : z => length(z)}
setting_summary  = {for k, v in settings : k => v if k != "climate"}
tasks_by_zone    = {for entry in care_log : entry.zone => entry.task...}

# --- templates: interpolation, %{if}/%{else}/%{endif}, %{for}/%{endfor} ---

greeting = "Hello, ${upper(title)}! Zones online: ${length(zones)}."

status_report = "Status: %{if is_open}open for visitors%{else}closed today%{endif}. Zones: %{for z in zones}[${z}]%{endfor}"

# --- heredoc, plain and with interpolation ---------------------------------

plain_notes = <<EOF
Static checklist, no interpolation here.
Line two.
EOF

daily_notes = <<-EOF
  Daily watering checklist:
  - start at ${zones[0]}
  - fern needs ${fern_water}L today
  EOF

# --- template trim markers ("~"): strip adjacent whitespace/newlines ------

trim_demo = "A   ${~title~}   B"

zone_report = <<-EOF
  %{for z in zones~}
  - ${z}
  %{endfor~}
  EOF

# --- function calls, including "..." last-argument expansion --------------

merged_zones      = concat(zones, ["compost"])
coordinate_groups = [[1, 2], [3, 4], [5, 6]]
flattened_groups  = concat(coordinate_groups...)
mixed_text        = concat("Capacity is ", capacity, ", open=", is_open)
shout             = upper(title)
whisper           = lower(shout)
zone_count        = length(zones)

# --- postfix chaining on non-identifier bases (call, paren, object, tuple,
#     for-expression, and splat combined with postfix) ---------------------

first_merged_zone  = concat(zones, ["compost"])[0]
active_humidity    = (is_open ? day_mode : night_mode).humidity
warm_temp          = { cold = 10, warm = 25 }["warm"]
second_coordinate  = [100, 200, 300][1]
loudest_short_zone = [for z in zones : upper(z)][0]
all_zone_labels    = concat(garden_zones, [])[*].label
