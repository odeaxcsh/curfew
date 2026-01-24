PREFIX ?= /usr/local
SYSCONFDIR ?= /etc
SYSTEMD_SYSTEM_DIR ?= /etc/systemd/system
SYSTEMD_USER_DIR ?= $(HOME)/.config/systemd/user

BINDIR := $(PREFIX)/bin
LIBDIR := $(PREFIX)/lib/curfew

INSTALL ?= install

install: install-system install-user
	@echo "Done. Now run: curfew apply"

install-system:
	@echo "Installing system files (requires root)..."
	sudo $(INSTALL) -d "$(BINDIR)" "$(LIBDIR)" "$(SYSTEMD_SYSTEM_DIR)"
	sudo $(INSTALL) -m 0755 bin/curfew "$(BINDIR)/curfew"
	sudo $(INSTALL) -m 0755 lib/curfew.sh "$(LIBDIR)/curfew.sh"
	@# Install default config only if missing
	@if [ ! -f "$(SYSCONFDIR)/curfew.conf" ]; then \
	  sudo $(INSTALL) -m 0644 config/curfew.conf "$(SYSCONFDIR)/curfew.conf"; \
	  echo "Installed default $(SYSCONFDIR)/curfew.conf"; \
	else \
	  echo "$(SYSCONFDIR)/curfew.conf exists; leaving as-is"; \
	fi
	sed -e "s|/usr/local|$(PREFIX)|g" systemd/system/curfew-enforce.service | sudo $(INSTALL) -m 0644 /dev/stdin "$(SYSTEMD_SYSTEM_DIR)/curfew-enforce.service"
	sed -e "s|/usr/local|$(PREFIX)|g" systemd/system/curfew-shutdown.service | sudo $(INSTALL) -m 0644 /dev/stdin "$(SYSTEMD_SYSTEM_DIR)/curfew-shutdown.service"

install-user:
	@if [ "$$(id -u)" -eq 0 ]; then \
	  echo "Refusing to install user units as root. Run 'make install-user' without sudo."; \
	  exit 1; \
	fi
	@echo "Installing user files (no sudo)..."
	$(INSTALL) -d "$(SYSTEMD_USER_DIR)"
	sed -e "s|/usr/local|$(PREFIX)|g" systemd/user/curfew-warn.service | $(INSTALL) -m 0644 /dev/stdin "$(SYSTEMD_USER_DIR)/curfew-warn.service"
	@echo "User unit installed to: $(SYSTEMD_USER_DIR)"

uninstall: uninstall-system uninstall-user
	@echo "Uninstalled. (Config $(SYSCONFDIR)/curfew.conf not removed.)"

uninstall-system:
	@echo "Removing system files (requires root)..."
	sudo rm -f "$(BINDIR)/curfew"
	sudo rm -rf "$(LIBDIR)"
	sudo rm -f "$(SYSTEMD_SYSTEM_DIR)/curfew-enforce.service"
	sudo rm -f "$(SYSTEMD_SYSTEM_DIR)/curfew-shutdown.service"
	sudo rm -f "$(SYSTEMD_SYSTEM_DIR)/curfew-shutdown.timer"

uninstall-user:
	@echo "Removing user files (no sudo)..."
	rm -f "$(SYSTEMD_USER_DIR)/curfew-warn.service"
	rm -f "$(SYSTEMD_USER_DIR)/curfew-warn.timer"

.PHONY: install install-system install-user uninstall uninstall-system uninstall-user
