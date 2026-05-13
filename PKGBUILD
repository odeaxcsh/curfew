pkgname=curfew
pkgver=1.0.0
pkgrel=1
pkgdesc='Systemd-based sleep curfew with D-Bus daemon, CLI and GTK4 UI'
arch=('x86_64')
license=('MIT')
depends=('systemd-libs' 'gtk4' 'libadwaita' 'polkit')
makedepends=('meson' 'gcc' 'pkgconf' 'systemd')
install=curfew.install
backup=('etc/curfew/curfew.conf')
source=()
sha256sums=()

build() {
  cd "$startdir"
  meson setup builddir --prefix=/usr --buildtype=release
  meson compile -C builddir
}

check() {
  cd "$startdir"
  meson test -C builddir --print-errorlogs
}

package() {
  cd "$startdir"
  DESTDIR="$pkgdir" meson install -C builddir

  # Install default config if not present
  install -Dm644 configs/curfew.conf.example \
    "$pkgdir/etc/curfew/curfew.conf"
}
