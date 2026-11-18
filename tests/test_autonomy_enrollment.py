"""Narrow source checks; these do not execute engine login or logout."""
from pathlib import Path
import unittest
ROOT = Path(__file__).resolve().parents[1]
class EnrollmentBoundaryTests(unittest.TestCase):
    def test_both_login_barriers_precede_materialization(self):
        source = (ROOT / 'src/Bot/PlayerbotMgr.cpp').read_text()
        ingress = source.split('void PlayerbotHolder::AddPlayerBot(', 1)[1].split('void PlayerbotHolder::AbandonPendingLogin', 1)[0]
        self.assertLess(ingress.index('AutonomyEnrollment::LoginBlocked'), ingress.index('holder->Initialize()'))
        callback = source.split('void PlayerbotHolder::HandlePlayerBotLoginCallback(', 1)[1].split('void PlayerbotHolder::UpdateSessions', 1)[0]
        self.assertLess(callback.index('AutonomyEnrollment::LoginBlocked'), callback.index('HandlePlayerLoginFromDB'))
        self.assertIn('AbandonPendingLogin(holder.GetGuid());', callback)
    def test_classification_is_a_cached_explicit_assignment(self):
        header = (ROOT / 'src/Bot/RandomPlayerbotMgr.h').read_text()
        source = (ROOT / 'src/Bot/RandomPlayerbotMgr.cpp').read_text()
        self.assertIn('return _dedicatedRandomAccounts.count(accountId) != 0;', header)
        population = source.split('// Populate filtered account lists', 1)[1].split('bool RandomPlayerbotMgr::IsAccountType', 1)[0]
        self.assertIn('if (accountType == 1)', population)
        self.assertIn('_dedicatedRandomAccounts.insert(accountId);', population)
        self.assertIn('_dedicatedRandomAccounts.clear();', source)
