#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
"""translatelangs.py with an index and models of its own (file: URLs, as
tests/systemd.sh uses them too): pairs through English, the download
checked and put in place, the job's progress and its failures.

    python3 -m unittest discover -s gateway -p 'test_*.py'
"""
import json
import os
import shutil
import sys
import tempfile
import unittest
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import translatelangs as tl  # noqa: E402


def fake_model(path, top, bad=False):
    with zipfile.ZipFile(path, "w") as z:
        z.writestr(top + "/metadata.json", "{}")
        z.writestr(top + "/sentencepiece.model", "sp")
        if not bad:
            z.writestr(top + "/model/model.bin", b"\0" * 1000)
        z.writestr(top + "/stanza/he/x.pt", "not wanted")


def fake_index(d, pairs, bad=()):
    index = []
    for a, b in pairs:
        z = os.path.join(d, "%s_%s.argosmodel" % (a, b))
        fake_model(z, "translate-%s_%s-1_0" % (a, b), bad=(a, b) in bad)
        index.append({"from_code": a, "to_code": b, "package_version": "1.0", "links": ["file://" + z]})
        index.append({"from_code": a, "to_code": b, "package_version": "0.9", "links": ["file:///old"]})
    with open(os.path.join(d, "index.json"), "w") as f:
        json.dump(index, f)
    return "file://" + os.path.join(d, "index.json")


class LangsTest(unittest.TestCase):
    def setUp(self):
        self.d = tempfile.mkdtemp(prefix="translatelangs.")
        self.addCleanup(shutil.rmtree, self.d)
        self.models = os.path.join(self.d, "models")
        self.state = os.path.join(self.d, "state")
        os.makedirs(self.state)
        self.index = fake_index(self.d, [("he", "en"), ("en", "he"), ("de", "en"), ("en", "de")],
                                bad={("en", "de")})

    def test_pairs_through_english(self):
        self.assertEqual(tl.pairs({"en", "he", "de"}), [("de", "en"), ("en", "de"), ("he", "en"), ("en", "he")])

    def test_table(self):
        rows = {c: (n, mb) for c, n, mb in tl.table()}
        self.assertEqual(rows["en"], ("English", 0))
        self.assertEqual(rows["he"], ("Hebrew", 230))
        self.assertEqual(tl.CAP_MB, 1108)

    def test_install_remove(self):
        self.assertTrue(tl.apply({"he"}, self.models, index=self.index))
        self.assertEqual(tl.installed(self.models), {"en", "he"})
        self.assertEqual(sorted(os.listdir(os.path.join(self.models, "he_en"))),
                         ["metadata.json", "model", "sentencepiece.model"])        # stanza left out
        self.assertFalse(tl.apply({"he", "en"}, self.models, index=self.index))   # nothing to do
        self.assertTrue(tl.apply(set(), self.models, index=self.index))
        self.assertEqual(tl.installed(self.models), {"en"})
        self.assertEqual(os.listdir(self.models), [])

    def test_job(self):
        with open(os.path.join(self.state, "request"), "w") as f:
            json.dump({"id": 3, "want": ["en", "he"]}, f)
        tl.job(self.state, self.models, index=self.index)
        self.assertEqual(tl.read_json(os.path.join(self.state, "job")), {"id": 3, "state": "done", "changed": True})
        # a broken download: failed, said in words, and nothing half there
        with open(os.path.join(self.state, "request"), "w") as f:
            json.dump({"id": 4, "want": ["he", "de"]}, f)
        tl.job(self.state, self.models, index=self.index)
        job = tl.read_json(os.path.join(self.state, "job"))
        self.assertEqual((job["id"], job["state"], job["why"]),
                         (4, "failed", "the en>de model isn't one translategate can use"))
        self.assertNotIn("en_de", os.listdir(self.models))
        self.assertEqual([n for n in os.listdir(self.models) if n.startswith(".part")], [])

    def test_refusals(self):
        with self.assertRaises(tl.Fail) as e:
            tl.apply({"de", "ru", "es", "et"}, self.models, index=self.index)
        self.assertEqual(str(e.exception), "those languages take 1276 MB; 1108 MB fit")
        with self.assertRaises(tl.Fail) as e:
            tl.apply({"fr"}, self.models, index=self.index)
        self.assertEqual(str(e.exception), "Argos Translate has no model for fr>en, en>fr")
        with self.assertRaises(tl.Fail):
            tl.apply({"xx"}, self.models, index=self.index)


if __name__ == "__main__":
    unittest.main()
