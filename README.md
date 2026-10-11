# mod-small-raid-fixes

An [AzerothCore](https://www.azerothcore.org/) (WotLK 3.3.5a) module that tones down
raid mechanics AutoBalance cannot scale, for groups smaller than the raid was made for.
Each fix is strongest for one player and fades out linearly, turning off at the
threshold (10 players by default), where AutoBalance handles everything.

- **Molten Core, Majordomo Executus:** less damage from the firepit's burning ground.
- **Molten Core, Ragnaros:** Wrath of Ragnaros knocks you back less often.
- **Blackwing Lair, Razorgore phase 1:** fewer living adds at once.

Works alongside [mod-autobalance](https://github.com/azerothcore/mod-autobalance).

## Installation

Clone into the core's `modules` directory, re-run CMake and rebuild, then copy
`conf/mod-small-raid-fixes.conf.dist` to your `etc/modules/` folder as
`mod-small-raid-fixes.conf`. Every fix and threshold is a setting there.
