#!/usr/bin/env python3
"""Stateful IMAP move/copy recovery over real TLS; no live accounts."""
import argparse
import base64
from datetime import datetime, timedelta, timezone
import re
import ssl
import subprocess
import tempfile
import threading
from pathlib import Path
from protocol_fixture import Handler, Server, State, RAW


class Mailbox:
    def __init__(self, scenario):
        self.scenario = scenario
        self.folders = {'INBOX': {1: (RAW, set()), 99: (RAW+b'unrelated', {'\\Deleted'})},
                        'Archive': {5: (RAW, set())}, 'Trash': {}}
        self.epochs = {'INBOX': 41, 'Archive': 77, 'Trash': 88}
        self.operations = []
        self.injected = False


class MoveHandler(Handler):
    def authenticate(self, token):
        parts = base64.b64decode(token).split(b'\0')
        self.authenticated = self.secure and parts[-1] == b'test-password'
        self.username = parts[-2].decode()
        if self.authenticated:
            self.box = self.server.mailboxes.setdefault(self.username, Mailbox(self.username))
        return self.authenticated

    def imap(self):
        self.send('* OK move fixture\r\n')
        selected = None
        while line := self.line():
            tag, command = line.split(' ', 1)
            if command == 'CAPABILITY':
                scenario = self.username
                capability = ('IMAP4rev2' if scenario == 'rev2' else 'IMAP4rev1 '+
                              ('' if scenario in ('no_uidplus', 'queued_cancel', 'delete_no_uidplus') else 'UIDPLUS ')+
                              ('MOVE' if scenario in ('atomic', 'move_disconnect', 'move_no_receipt') else ''))
                self.send(f'* CAPABILITY {capability} AUTH=PLAIN SASL-IR\r\n{tag} OK capabilities\r\n')
            elif command.startswith('AUTHENTICATE PLAIN'):
                fields = command.split()
                if len(fields) == 2:
                    self.send('+ \r\n')
                    fields.append(self.line())
                self.send(f'{tag} {"OK" if self.authenticate(fields[2]) else "NO"} authentication\r\n')
            elif command.startswith('LOGIN '):
                fields = [a or b for a, b in re.findall(r'"([^"]*)"|(\S+)', command[6:])]
                ok = self.authenticate(base64.b64encode(('\0'+'\0'.join(fields)).encode()))
                self.send(f'{tag} {"OK" if ok else "NO"} authentication\r\n')
            elif command == 'LOGOUT':
                self.send(f'* BYE\r\n{tag} OK logout\r\n')
                return
            elif not self.authenticated:
                raise AssertionError('command before authentication')
            elif command.startswith('LIST '):
                for folder, role in [('INBOX', ''), ('Archive', '\\Archive'), ('Trash', '\\Trash')]:
                    self.send(f'* LIST ({role}) "/" "{folder}"\r\n')
                self.send(f'{tag} OK list\r\n')
            elif command.startswith('SELECT '):
                selected = command[7:].strip('"')
                self.send(f'* {len(self.box.folders[selected])} EXISTS\r\n'
                          f'* OK [UIDVALIDITY {self.box.epochs[selected]}] stable\r\n{tag} OK selected\r\n')
            elif command.startswith('UID FETCH '):
                requested, fields = command[10:].split(' ', 1)
                items = self.box.folders[selected]
                for index, (uid, (raw, flags)) in enumerate(list(items.items()), 1):
                    if requested != '1:*' and int(requested) != uid:
                        continue
                    if 'BODY' in fields:
                        assert 'BODY.PEEK[]' in fields, command
                        self.send(f'* {index} FETCH (UID {uid} BODY[] {{{len(raw)}}}\r\n'.encode()+raw+b')\r\n')
                    else:
                        self.send(f'* {index} FETCH (UID {uid} FLAGS ({" ".join(sorted(flags))}))\r\n')
                self.send(f'{tag} OK fetched\r\n')
            elif command.startswith(('UID COPY ', 'UID MOVE ')):
                match = re.fullmatch(r'UID (COPY|MOVE) (\d+) "([^"]+)"', command)
                verb, source, target = match.groups()
                source = int(source)
                scenario = self.box.scenario
                first = not self.box.injected
                self.box.injected = True
                self.box.operations.append((verb, selected, source, target))
                if first and scenario in ('no_copy', 'retry_missing'):
                    return
                if first and scenario == 'copy_no':
                    self.send(f'{tag} NO quota\r\n')
                    continue
                raw, flags = self.box.folders[selected][source]
                uid = max(self.box.folders[target], default=0)+1
                self.box.folders[target][uid] = (raw, flags.copy())
                if verb == 'MOVE':
                    del self.box.folders[selected][source]
                if first and scenario in ('duplicate', 'retry_duplicate', 'keep_local', 'retry_reset_source'):
                    self.box.folders[target][uid+1] = (raw, flags.copy())
                if first and scenario == 'different_bytes':
                    self.box.folders[target][uid] = (raw+b'different content, same Message-ID', flags.copy())
                if first and scenario == 'target_reset':
                    self.box.epochs[target] = 78
                if first and scenario in ('source_reset', 'retry_reset_source'):
                    self.box.epochs[selected] = 42
                if scenario == 'retry_duplicate' or (first and scenario in ('copy_disconnect', 'move_disconnect', 'duplicate', 'different_bytes', 'target_reset', 'keep_local', 'retry_reset_source')):
                    return
                if first and scenario == 'bad_receipt':
                    self.send(f'{tag} OK [COPYUID 77 999 {uid}] copied\r\n')
                elif first and scenario == 'conflicting_receipt':
                    self.send(f'* OK [COPYUID 77 {source} {uid}] copied\r\n'
                              f'{tag} OK [COPYUID 77 {source} {uid+1}] copied\r\n')
                elif scenario in ('copy_no_receipt', 'move_no_receipt'):
                    self.send(f'{tag} OK copied\r\n')
                else:
                    self.send(f'{tag} OK [COPYUID {self.box.epochs[target]} {source} {uid}] copied\r\n')
            elif command.startswith('UID STORE '):
                match = re.fullmatch(r'UID STORE (\d+) ([+-])FLAGS\.SILENT \((\\\w+)\)', command)
                uid, operation, flag = match.groups()
                self.box.operations.append(('STORE', selected, int(uid), flag))
                flags = self.box.folders[selected][int(uid)][1]
                flags.add(flag) if operation == '+' else flags.discard(flag)
                if self.box.scenario == 'store_disconnect' and flag == '\\Deleted' and not getattr(self.box, 'stored', False):
                    self.box.stored = True
                    return
                self.send(f'{tag} OK stored\r\n')
            elif command.startswith('UID EXPUNGE '):
                uid = int(command[12:])
                self.box.operations.append(('EXPUNGE', selected, uid))
                item = self.box.folders[selected].get(uid)
                if item and '\\Deleted' in item[1]:
                    del self.box.folders[selected][uid]
                if self.box.scenario == 'expunge_disconnect' and not getattr(self.box, 'expunged', False):
                    self.box.expunged = True
                    return
                self.send(f'{tag} OK expunged\r\n')
            else:
                raise AssertionError('unexpected command: '+command)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('executable')
    parser.add_argument('--native-ui', action='store_true')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix='move-tls-', dir=root/'.cache') as temporary:
        directory = Path(temporary)
        certificate, key = directory/'cert.pem', directory/'key.pem'
        # A disposable QEMU guest may trail the host clock by a few seconds.
        # Backdate this one-day test certificate; keep normal TLS verification.
        (directory/'index').touch()
        (directory/'serial').write_text('01\n')
        config = directory/'ca.conf'
        config.write_text('[ca]\ndefault_ca=fixture\n[fixture]\n'
                          f'database={directory}/index\nnew_certs_dir={directory}\nserial={directory}/serial\n'
                          'default_md=sha256\npolicy=policy\nx509_extensions=extensions\n'
                          '[policy]\ncommonName=supplied\n[extensions]\n'
                          'subjectAltName=IP:127.0.0.1,IP:10.0.2.2\nbasicConstraints=critical,CA:TRUE\n')
        request = directory/'request.pem'
        subprocess.run(['openssl', 'req', '-new', '-newkey', 'rsa:2048', '-nodes',
                        '-subj', '/CN=Mail move fixture', '-keyout', str(key), '-out', str(request)],
                       check=True, capture_output=True)
        subprocess.run(['openssl', 'ca', '-selfsign', '-batch', '-notext', '-config', str(config),
                        '-keyfile', str(key), '-in', str(request), '-out', str(certificate), '-days', '1',
                        '-startdate', (datetime.now(timezone.utc)-timedelta(minutes=5)).strftime('%Y%m%d%H%M%SZ')],
                       check=True, capture_output=True)
        tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        tls.load_cert_chain(certificate, key)
        server = Server(('0.0.0.0', 0), MoveHandler)
        server.implicit, server.protocol, server.tls = True, 'imap', tls
        server.state, server.mailboxes = State(), {}
        threading.Thread(target=server.serve_forever, daemon=True).start()
        try:
            result = subprocess.run([args.executable, '127.0.0.1', str(certificate),
                                     *([str(server.server_address[1])]*6)], timeout=180)
            assert not server.state.errors, server.state.errors
            if result.returncode:
                print('Recorded move operations:', {name: box.operations for name, box in server.mailboxes.items()}, flush=True)
            assert result.returncode == 0, result.returncode
            assert len(server.mailboxes) == (3 if args.native_ui else 25), server.mailboxes.keys()
            for scenario, box in server.mailboxes.items():
                copies = [op for op in box.operations if op[0] in ('COPY', 'MOVE')]
                assert len(copies) == (0 if scenario in ('no_uidplus', 'queued_cancel') else 2 if scenario in ('copy_no', 'newer_edit', 'retry_missing', 'retry_duplicate') else 1), (scenario, copies)
                assert box.folders['Archive'][5][0] == RAW, scenario
                assert box.folders['INBOX'][99] == (RAW+b'unrelated', {'\\Deleted'}), scenario
                for op in box.operations:
                    if op[0] == 'EXPUNGE':
                        assert op[2] != 99, (scenario, op)
                if scenario in ('no_copy', 'duplicate', 'different_bytes', 'target_reset', 'source_reset', 'no_uidplus', 'retry_reset_source', 'keep_local', 'queued_cancel'):
                    assert 1 in box.folders['INBOX'], scenario
                else:
                    assert 1 not in box.folders['INBOX'], scenario
                if scenario == 'retry_duplicate':
                    assert set(box.folders['Archive']) == {5, 6, 7, 8}, box.folders
            print('Move server checks passed: exact copy counts, targeted expunge, old duplicate and unrelated deleted mail preserved.')
        finally:
            server.shutdown()
            server.server_close()


if __name__ == '__main__':
    main()
