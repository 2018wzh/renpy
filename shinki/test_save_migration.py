import os
from pathlib import Path
import tempfile
import unittest
import zipfile

from shinki_save_migration import legacy_save_root, migrate_legacy_saves


class SaveMigrationTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        variable = "LOCALAPPDATA" if os.name == "nt" else "HOME"
        previous = os.environ.get(variable)
        os.environ[variable] = str(self.root / "appdata")

        def restore():
            if previous is None:
                os.environ.pop(variable, None)
            else:
                os.environ[variable] = previous

        self.addCleanup(restore)
        self.game = self.root / "game Ω"
        self.legacy = legacy_save_root(str(self.game)) / "saves"
        self.legacy.mkdir(parents=True)
        self.local = self.game / "game" / "saves"
        self.native = self.root / "RenPy" / "title"
        self.tokens = self.root / "RenPy" / "tokens"
        self.backups = self.root / "RenPy" / "backups"

    def migrate(self):
        migrate_legacy_saves(
            str(self.game), [self.native, self.local], self.tokens, self.backups
        )

    def test_archive_tokens_and_backups_remain_byte_exact(self):
        archive = self.legacy / "1-1-LT1.save"
        with zipfile.ZipFile(archive, "w") as saved:
            saved.writestr("log", b"\x80\x02N.")
            saved.writestr("json", b'{"_save_name":"migration"}')
            saved.writestr("signatures", b"original signature bytes\n")
        payload = archive.read_bytes()
        (self.legacy / "tokens").mkdir()
        (self.legacy / "tokens" / "security_keys.txt").write_bytes(b"original key bytes\n")
        (self.legacy / "backups").mkdir()
        (self.legacy / "backups" / "persistent.bak").write_bytes(b"\x78\x9c\x00\xff")
        self.migrate()
        for destination in [self.native, self.local]:
            self.assertEqual((destination / archive.name).read_bytes(), payload)
            with zipfile.ZipFile(destination / archive.name) as saved:
                self.assertEqual(saved.read("log"), b"\x80\x02N.")
        self.assertEqual((self.tokens / "security_keys.txt").read_bytes(), b"original key bytes\n")
        self.assertEqual((self.backups / "persistent.bak").read_bytes(), b"\x78\x9c\x00\xff")
        self.assertEqual(archive.read_bytes(), payload)

    def test_existing_save_in_either_native_directory_wins(self):
        for existing in [self.native, self.local]:
            existing.mkdir(parents=True)
        (self.native / "slot.save").write_bytes(b"native slot")
        (self.local / "persistent").write_bytes(b"native persistent")
        (self.legacy / "slot.save").write_bytes(b"old slot")
        (self.legacy / "persistent").write_bytes(b"old persistent")
        self.tokens.mkdir(parents=True)
        (self.tokens / "security_keys.txt").write_bytes(b"native token")
        (self.legacy / "tokens").mkdir()
        (self.legacy / "tokens" / "security_keys.txt").write_bytes(b"old token")
        self.migrate()
        self.assertEqual((self.native / "slot.save").read_bytes(), b"native slot")
        self.assertFalse((self.local / "slot.save").exists())
        self.assertEqual((self.local / "persistent").read_bytes(), b"native persistent")
        self.assertFalse((self.native / "persistent").exists())
        self.assertEqual((self.tokens / "security_keys.txt").read_bytes(), b"native token")

    def test_completed_migration_does_not_resurrect_deleted_saves(self):
        (self.legacy / "slot.save").write_bytes(b"old save")
        self.migrate()
        for target in [self.native, self.local]:
            (target / "slot.save").unlink()
        (self.legacy / "later.save").write_bytes(b"late old save")
        self.migrate()
        for target in [self.native, self.local]:
            self.assertFalse((target / "slot.save").exists())
            self.assertFalse((target / "later.save").exists())

    def test_incomplete_copy_does_not_mark_migration_complete(self):
        (self.legacy / "slot.save").write_bytes(b"old save")
        self.native.parent.mkdir(parents=True)
        self.native.write_bytes(b"not a directory")
        with self.assertRaises(OSError):
            self.migrate()
        self.assertFalse((self.legacy.parent / ".native-save-migration-complete").exists())
        self.assertEqual((self.legacy / "slot.save").read_bytes(), b"old save")


if __name__ == "__main__":
    unittest.main()
