#!/usr/bin/env bash
# Adapter for tests/protocol_fixture.py: only connects the disposable guest to
# this host's ephemeral protocol servers, using a temporary fixture certificate.
set -euo pipefail
MAIL_ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
MAIL_VM_PORT=${MAIL_VM_PORT:-2258}
MAIL_VM_KEY=${MAIL_VM_KEY:-$MAIL_ROOT/../summit/.vm/airos-x86_64/id_ed25519}
MAIL_VM_KNOWN_HOSTS=${MAIL_VM_KNOWN_HOSTS:-$MAIL_ROOT/.cache/vm/known_hosts}
MAIL_BUILD=${MAIL_BUILD:-$MAIL_ROOT/build-haiku}
MAIL_NATIVE_TEST=${MAIL_NATIVE_TEST:-mail_protocol_tests}
case "$MAIL_NATIVE_TEST" in mail_protocol_tests|mail_move_tests|mail_delete_tests|mail_imap_draft_tests|mail_native_accounts|mail_native_outbox|mail_sent_tests|mail_native_sent|mail_native_moves) ;; *) exit 2;; esac
MAIL_SSH=(-i "$MAIL_VM_KEY" -o IdentitiesOnly=yes -o BatchMode=yes
    -o "UserKnownHostsFile=$MAIL_VM_KNOWN_HOSTS" -o ConnectTimeout=10)
[[ $# == 8 ]] || exit 2
MAIL_CA=$2
shift 2
for port in "$@"; do [[ "$port" =~ ^[0-9]{1,5}$ ]] || exit 2; done
scp -O "${MAIL_SSH[@]}" -P "$MAIL_VM_PORT" "$MAIL_BUILD/$MAIL_NATIVE_TEST" \
    "user@127.0.0.1:/boot/home/mail-check/$MAIL_NATIVE_TEST.new"
scp -O "${MAIL_SSH[@]}" -P "$MAIL_VM_PORT" "$MAIL_CA" \
    user@127.0.0.1:/boot/home/mail-check/protocol-cert.pem
ssh "${MAIL_SSH[@]}" -p "$MAIL_VM_PORT" user@127.0.0.1 \
    "cd /boot/home/mail-check && mv $MAIL_NATIVE_TEST.new $MAIL_NATIVE_TEST && MAIL_ACCOUNTS_SCREENSHOT=/boot/home/mail-check/accounts.png MAIL_COMPOSER_SCREENSHOT=/boot/home/mail-check/composer.png MAIL_OUTBOX_SCREENSHOT=/boot/home/mail-check/outbox.png ./$MAIL_NATIVE_TEST 10.0.2.2 /boot/home/mail-check/protocol-cert.pem $* > $MAIL_NATIVE_TEST.log 2>&1; mail_test_status=\$?; cat $MAIL_NATIVE_TEST.log; exit \$mail_test_status"
if [[ "$MAIL_NATIVE_TEST" == mail_native_outbox ]]; then
    scp -O "${MAIL_SSH[@]}" -P "$MAIL_VM_PORT" user@127.0.0.1:/boot/home/mail-check/outbox.png "$MAIL_ROOT/.cache/vm/outbox.png"
fi

if [[ "$MAIL_NATIVE_TEST" == mail_native_sent ]]; then
    scp -O "${MAIL_SSH[@]}" -P "$MAIL_VM_PORT" user@127.0.0.1:/boot/home/mail-check/sent.png "$MAIL_ROOT/.cache/vm/sent.png"
fi
if [[ "$MAIL_NATIVE_TEST" == mail_native_moves ]]; then
    scp -O "${MAIL_SSH[@]}" -P "$MAIL_VM_PORT" user@127.0.0.1:/boot/home/mail-check/moves.png "$MAIL_ROOT/.cache/vm/moves.png"
fi
