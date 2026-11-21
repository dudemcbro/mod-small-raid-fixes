# mod-small-raid-fixes

<img src="icon.png" alt="" width="64" align="right">

An [AzerothCore](https://www.azerothcore.org/) (WotLK 3.3.5a) module that tones down
raid mechanics AutoBalance cannot scale, for groups smaller than the raid was made for.
Each fix is strongest for one player and fades out linearly, turning off at the
threshold (10 players by default), where AutoBalance handles everything.

- **Author:** [dudemcbro](https://github.com/dudemcbro)
- **License:** [GNU AGPL v3.0](LICENSE)
- **Works with:** AzerothCore WotLK (3.3.5a). The module only uses standard core APIs,
  with no core patches. One world SQL file binds the Ragnaros fix to its spell.

## Features

- **Molten Core, Majordomo Executus:** less damage from the firepit's burning ground.
- **Molten Core, Ragnaros:** Wrath of Ragnaros knocks you back less often.
- **Blackwing Lair, Razorgore phase 1:** fewer living adds at once.

Works alongside [mod-autobalance](https://github.com/azerothcore/mod-autobalance).

## Quick start

1. Clone the module into the core's `modules` folder:
   ```sh
   cd <azerothcore>/modules
   git clone https://github.com/dudemcbro/mod-small-raid-fixes.git
   ```
2. Re-run CMake and rebuild the core, for example with `./acore.sh compiler build`.
3. Set up the config file (below) and restart `worldserver`. It imports
   `data/sql/world` on startup (with the updater enabled, the default). The file can run
   again safely.

## Configuration

The build installs `mod-small-raid-fixes.conf.dist` into the `modules` folder next to
`worldserver.conf`. Copy it to `mod-small-raid-fixes.conf` to change a setting; without
it the defaults are used. Every fix can be turned off on its own, and each raid has its
own player threshold. The file describes each option.

## Updating

```sh
cd <azerothcore>/modules/mod-small-raid-fixes
git pull
```

Then re-run CMake, rebuild and restart `worldserver`. Compare your
`mod-small-raid-fixes.conf` with the new `.conf.dist` and add any new options.

## Removing the module

Delete its folder, re-run CMake and rebuild. The leftover `spell_script_names` row for
spell 20566 only causes a startup warning; delete it to clear that.

## Contributing

Bug reports and pull requests are welcome on
[GitHub](https://github.com/dudemcbro/mod-small-raid-fixes). For larger changes, please
open an issue first, and describe how you tested your change.

## License

Licensed under the GNU Affero General Public License v3.0. See [LICENSE](LICENSE).
