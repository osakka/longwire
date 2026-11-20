// SPDX-License-Identifier: GPL-3.0-or-later
package org.longwire;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.security.KeyStore;
import java.security.SecureRandom;
import java.util.Arrays;
import javax.crypto.Cipher;
import javax.crypto.KeyGenerator;
import javax.crypto.SecretKey;
import javax.crypto.spec.GCMParameterSpec;
import android.security.keystore.KeyGenParameterSpec;
import android.security.keystore.KeyProperties;

/**
 * The phone's identity seed (docs/adr/0091): 32 bytes, kept encrypted by a key that lives in the Android Keystore and cannot be taken out of it. The file holds the nonce and the
 * ciphertext; read alone it is nothing. The seed goes to the engine in memory and is wiped after; no plaintext seed file is made.
 */
final class SeedStore {
    private static final String ALIAS = "longwire-seed";
    private static final String FILE = "seed.enc";

    private SeedStore() {}

    private static SecretKey key() throws Exception {
        KeyStore ks = KeyStore.getInstance("AndroidKeyStore");
        ks.load(null);
        if (ks.containsAlias(ALIAS)) {
            return (SecretKey) ks.getKey(ALIAS, null);
        }
        KeyGenerator g = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, "AndroidKeyStore");
        g.init(new KeyGenParameterSpec.Builder(ALIAS, KeyProperties.PURPOSE_ENCRYPT | KeyProperties.PURPOSE_DECRYPT)
            .setBlockModes(KeyProperties.BLOCK_MODE_GCM).setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE).setKeySize(256).build());
        return g.generateKey();
    }

    /** The seed: decrypted from the store, or made new if there is none yet. The caller wipes the array. */
    static byte[] load(String dir) throws Exception {
        File f = new File(dir, FILE);
        if (f.exists()) {
            byte[] all = new byte[(int) f.length()];
            try (FileInputStream in = new FileInputStream(f)) {
                if (in.read(all) != all.length || all.length < 12 + 32 + 16) {
                    throw new Exception("the stored identity is damaged");
                }
            }
            Cipher c = Cipher.getInstance("AES/GCM/NoPadding");
            c.init(Cipher.DECRYPT_MODE, key(), new GCMParameterSpec(128, all, 0, 12));
            byte[] seed = c.doFinal(all, 12, all.length - 12);
            if (seed.length != 32) {
                throw new Exception("the stored identity is not a seed");
            }
            return seed;
        }
        byte[] seed = new byte[32];
        File old = new File(dir, "seed");		// an install from before the keystore: its seed is taken in once, and the file is gone (a one-time step, not a mode)
        boolean moved = false;
        if (old.exists() && old.length() == 32) {
            try (FileInputStream in = new FileInputStream(old)) {
                moved = in.read(seed) == 32;
            }
        }
        if (!moved) {
            new SecureRandom().nextBytes(seed);
        }
        store(dir, seed);
        if (moved && !old.delete()) {
            old.deleteOnExit();
        }
        return seed;
    }

    /** Keep this seed (it replaces the one there is). */
    static void store(String dir, byte[] seed) throws Exception {
        Cipher c = Cipher.getInstance("AES/GCM/NoPadding");
        c.init(Cipher.ENCRYPT_MODE, key());
        byte[] ct = c.doFinal(seed);
        byte[] iv = c.getIV();
        File tmp = new File(dir, FILE + ".tmp");
        try (FileOutputStream out = new FileOutputStream(tmp)) {
            out.write(iv);
            out.write(ct);
            out.getFD().sync();
        }
        if (!tmp.renameTo(new File(dir, FILE))) {
            throw new Exception("cannot keep the identity");
        }
        Arrays.fill(ct, (byte) 0);
    }
}
