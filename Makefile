PREFIX ?= /usr/local
SYSTEMD_SYSTEM_DIR ?= /etc/systemd/system

BINDIR := $(PREFIX)/sbin
INSTALL ?= install

SERVICE := curfew.service
TIMER   := curfew.timer
SCRIPT  := curfew-service.sh
CLI     := curfew

install:
	sudo $(INSTALL) -d "$(BINDIR)" "$(SYSTEMD_SYSTEM_DIR)"
	sudo $(INSTALL) -m 0755 "$(SCRIPT)" "$(BINDIR)/${SCRIPT}"
	sudo $(INSTALL) -m 0755 "$(CLI)" "$(BINDIR)/$(CLI)"
	sudo $(INSTALL) -m 0644 "$(SERVICE)" "$(SYSTEMD_SYSTEM_DIR)/$(SERVICE)"
	sudo $(INSTALL) -m 0644 "$(TIMER)"   "$(SYSTEMD_SYSTEM_DIR)/$(TIMER)"
	sudo systemctl daemon-reload

uninstall:
	sudo systemctl disable --now curfew.timer >/dev/null 2>&1 || true
	sudo rm -f "$(BINDIR)/${SCRIPT}"
	sudo rm -f "$(BINDIR)/$(CLI)"
	sudo rm -f "$(SYSTEMD_SYSTEM_DIR)/$(SERVICE)"
	sudo rm -f "$(SYSTEMD_SYSTEM_DIR)/$(TIMER)"
	sudo rm -rf "$(SYSTEMD_SYSTEM_DIR)/curfew.service.d"
	sudo systemctl daemon-reload

.PHONY: install uninstall
