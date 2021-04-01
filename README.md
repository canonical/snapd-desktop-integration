# snapd-desktop-integration
User session helpers for snapd

## Installation:

Until various supporting features for this land in snapd (such as user daemons and a theme install slot), installation of `snapd-desktop-integration` will be a little roundabout:

```
snap set system experimental.user-daemons=true
snap install snapd-desktop-integration --edge
snap connect snapd-desktop-integration:snapd-control
```
