#!/usr/bin/env python3
"""Permanent deletion from Trash against the stateful IMAP move fixture; no live accounts."""
import argparse
from datetime import datetime, timedelta, timezone
import ssl
import subprocess
import tempfile
import threading
from pathlib import Path
from protocol_fixture import Server, State, RAW
from move_fixture import MoveHandler


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('executable')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    (root/'.cache').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='delete-tls-', dir=root/'.cache') as temporary:
        directory = Path(temporary)
        certificate, key = directory/'cert.pem', directory/'key.pem'
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
                        '-subj', '/CN=Mail delete fixture', '-keyout', str(key), '-out', str(request)],
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
                                     *([str(server.server_address[1])]*6)], timeout=120)
            assert not server.state.errors, server.state.errors
            if result.returncode:
                print('Recorded operations:', {name: box.operations for name, box in server.mailboxes.items()}, flush=True)
            assert result.returncode == 0, result.returncode
            assert len(server.mailboxes) == 5, server.mailboxes.keys()
            for scenario, box in server.mailboxes.items():
                operations = box.operations
                copies = [op for op in operations if op[0] in ('COPY', 'MOVE')]
                stores = [op for op in operations if op[0] == 'STORE' and op[3] == '\\Deleted']
                expunges = [op for op in operations if op[0] == 'EXPUNGE']
                # Other clients' deleted mail and other folders are never touched.
                assert box.folders['INBOX'][99] == (RAW+b'unrelated', {'\\Deleted'}), scenario
                assert box.folders['Archive'] == {5: (RAW, set())}, scenario
                assert all(op[2] == 1 for op in stores+expunges), (scenario, operations)
                if scenario == 'delete_trash':
                    # Moved to Trash first (one copy), then removed from Trash.
                    assert len(copies) == 1 and box.folders['Trash'] == {} and 1 not in box.folders['INBOX'], (scenario, box.folders)
                    assert ('EXPUNGE', 'Trash', 1) in expunges, (scenario, operations)
                elif scenario == 'delete_no_uidplus':
                    # Marked once, never expunged: a plain EXPUNGE would take UID 99 too.
                    assert not copies and not expunges and len(stores) == 1, (scenario, operations)
                    assert box.folders['INBOX'][1] == (RAW, {'\\Deleted'}), (scenario, box.folders)
                else:
                    # Deleted before the queued move ran: removed in place, no copy made.
                    assert not copies and box.folders['Trash'] == {} and 1 not in box.folders['INBOX'], (scenario, box.folders)
                    assert ('EXPUNGE', 'INBOX', 1) in expunges, (scenario, operations)
            print('Delete server checks passed: targeted expunge only, no copies of deleted mail, unrelated deleted mail preserved.')
        finally:
            server.shutdown()
            server.server_close()


if __name__ == '__main__':
    main()
