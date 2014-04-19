/**
 * This file is part of the CernVM File System
 *
 * This tool signs a CernVM-FS manifest with an X.509 certificate.
 */

#include "cvmfs_config.h"
#include "swissknife_letter.h"

#include <inttypes.h>
#include <termios.h>

#include "download.h"
#include "hash.h"
#include "letter.h"
#include "signature.h"
#include "util.h"
#include "whitelist.h"

using namespace std;  // NOLINT

int swissknife::CommandLetter::Main(const swissknife::ArgumentList &args) {
  bool verify = false;
  if (args.find('v') != args.end()) verify = true;
  if ((args.find('s') != args.end()) && verify) {
    LogCvmfs(kLogCvmfs, kLogStderr,
             "invalid option combination (sign + verify)");
    return 1;
  }

  bool loop = false;
  string repository_url;
  string certificate_path;
  string certificate_password;
  shash::Algorithms hash_algorithm;
  uint64_t max_age;
  if (verify) {
    repository_url = *args.find('r')->second;
    max_age = String2Uint64(*args.find('m')->second);
    if (args.find('l') != args.end()) loop = true;
  } else {
    certificate_path = *args.find('c')->second;
    if (args.find('p') != args.end())
      certificate_password = *args.find('p')->second;
    if (args.find('a') != args.end()) {
      hash_algorithm = shash::ParseHashAlgorithm(*args.find('a')->second);
      if (hash_algorithm == shash::kAny) {
        LogCvmfs(kLogCvmfs, kLogStderr, "unknown hash algorithm");
        return 1;
      }
    }
  }
  string fqrn;
  string text;
  string key_path;
  string cacrl_path;
  fqrn = *args.find('f')->second;
  key_path = *args.find('k')->second;
  if (args.find('t') != args.end()) text = *args.find('t')->second;
  if (args.find('z') != args.end()) cacrl_path = *args.find('z')->second;

  int retval;
  signature::SignatureManager signature_manager;
  signature_manager.Init();

  if (verify) {
    if (cacrl_path != "") {
      retval = signature_manager.LoadTrustedCaCrl(cacrl_path);
      if (!retval) {
        LogCvmfs(kLogCvmfs, kLogStderr, "failed to load CA/CRLs");
        return 2;
      }
    }
    retval = signature_manager.LoadPublicRsaKeys(key_path);
    if (!retval && (cacrl_path == "")) {
      LogCvmfs(kLogCvmfs, kLogStderr, "failed to load public keys");
      return 2;
    }

    download::DownloadManager download_manager;
    download_manager.Init(2, false);
    whitelist::Whitelist whitelist(fqrn, &download_manager, &signature_manager);
    retval = whitelist.Load(repository_url);
    if (retval != whitelist::kFailOk) {
      LogCvmfs(kLogCvmfs, kLogStderr, "failed to load whitelist (%d): %s",
               retval, whitelist::Code2Ascii((whitelist::Failures)retval));
      return 2;
    }

    int exit_code = 0;
    do {
      if (text == "") {
        char c;
        while ((retval = read(0, &c, 1)) == 1) {
          if (c == '\n')
            break;
          text.push_back(c);
        }
        if (retval != 1) return exit_code;
      }

      if ((time(NULL) + 3600*24*3) > whitelist.expires()) {
        whitelist::Whitelist refresh(fqrn, &download_manager,
                                     &signature_manager);
        retval = refresh.Load(repository_url);
        if (retval == whitelist::kFailOk)
          whitelist = refresh;
      }

      string message;
      string cert;
      letter::Letter letter(fqrn, text, &signature_manager);
      retval = letter.Verify(max_age, &message, &cert);
      if (retval != letter::kFailOk) {
        exit_code = 3;
      } else {
        if (whitelist.IsExpired()) {
          exit_code = 4;
        } else {
          exit_code = 5;
          retval = whitelist.VerifyLoadedCertificate();
          if (retval == whitelist::kFailOk)
            exit_code = 0;
        }
      }

      if (loop) {
        LogCvmfs(kLogCvmfs, kLogStdout, "%d", exit_code);
        if (exit_code == 0)
          LogCvmfs(kLogCvmfs, kLogStdout, "%u", message.length());
      }
      if (exit_code == 0)
        LogCvmfs(kLogCvmfs, kLogStdout, "%s", message.c_str());
      text = "";
    } while (loop);
    download_manager.Fini();
    signature_manager.Fini();
    return exit_code;
  }

  // Load certificate
  if (!signature_manager.LoadCertificatePath(certificate_path)) {
    LogCvmfs(kLogCvmfs, kLogStderr, "failed to load certificate");
    return 2;
  }

  // Load private key
  if (!signature_manager.LoadPrivateKeyPath(key_path, certificate_password)) {
    int retry = 0;
    bool success;
    do {
      struct termios defrsett, newrsett;
      tcgetattr(fileno(stdin), &defrsett);
      newrsett = defrsett;
      newrsett.c_lflag &= ~ECHO;
      if(tcsetattr(fileno(stdin), TCSAFLUSH, &newrsett) != 0) {
        LogCvmfs(kLogCvmfs, kLogStderr, "terminal failure");
        return 2;
      }

      LogCvmfs(kLogCvmfs, kLogStdout | kLogNoLinebreak,
               "Enter password for private key: ");
      certificate_password = "";
      GetLineFd(0, &certificate_password);
      tcsetattr(fileno(stdin), TCSANOW, &defrsett);
      LogCvmfs(kLogCvmfs, kLogStdout, "");

      success =
        signature_manager.LoadPrivateKeyPath(key_path, certificate_password);
      if (!success) {
        LogCvmfs(kLogCvmfs, kLogStderr, "failed to load private key (%s)",
                 signature_manager.GetCryptoError().c_str());
      }
      retry++;
    } while (!success && (retry < 3));
    if (!success)
      return 2;
  }
  if (!signature_manager.KeysMatch()) {
    LogCvmfs(kLogCvmfs, kLogStderr,
             "the private key doesn't seem to match your certificate (%s)",
             signature_manager.GetCryptoError().c_str());
    return 2;
  }
  if (text == "") {
    char c;
    while (read(0, &c, 1) == 1) {
      if (c == '\n')
        break;
      text.push_back(c);
    }
  }

  letter::Letter text_letter(fqrn, text, &signature_manager);
  LogCvmfs(kLogCvmfs, kLogStdout, "%s",
           text_letter.Sign(hash_algorithm).c_str());

  signature_manager.Fini();
  return 0;
}
