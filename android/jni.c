/* SPDX-License-Identifier: GPL-3.0-or-later
 * The engine inside the app (docs/adr/0076): liblongwire.so is this file, which is the whole engine and the three calls the app makes of it. The engine runs on a thread of the app's own process
 * (embedded: it closes none of the app's descriptors, takes no signals, and puts no filter on a process that is not its own), as a node whose port is the phone's adapter (engine/member.c). Everything
 * else the app asks of it goes through its control socket in the app's private directory, the same words the desktop's scripts use: dial, pickup, hangup, status, member, watch. */
#include <jni.h>
#include <pthread.h>
#include "../engine/all.c"

static lw_node *g_node;
static pthread_t g_thr;

static void *serve(void *a)
{
	(void)a;
	lw_node_serve(g_node);
	return 0;
}

/* "" when it is running, or what stopped it */
JNIEXPORT jstring JNICALL Java_org_longwire_Engine_start(JNIEnv *env, jclass cls, jstring jdir, jstring jsb, jstring jtoken, jstring jname, jboolean listed, jint keepalive, jbyteArray jseed)
{
	const char *dir = (*env)->GetStringUTFChars(env, jdir, 0), *sb = (*env)->GetStringUTFChars(env, jsb, 0);
	const char *tok = (*env)->GetStringUTFChars(env, jtoken, 0), *nm = (*env)->GetStringUTFChars(env, jname, 0);
	lw_cfg c;
	char err[300] = "";
	jstring r;

	(void)cls;
	if (g_node) {
		r = (*env)->NewStringUTF(env, "already running");
		goto done;
	}
	memset(&c, 0, sizeof c);
	if (!jseed || (*env)->GetArrayLength(env, jseed) != 32) {
		r = (*env)->NewStringUTF(env, "the app gave no identity");
		goto done;
	}
	(*env)->GetByteArrayRegion(env, jseed, 0, 32, (jbyte *)c.seed_mem);
	c.has_seed_mem = 1;
	snprintf(c.seed_path, sizeof c.seed_path, "%s/seed", dir);
	snprintf(c.ctl_path, sizeof c.ctl_path, "%s/ctl", dir);
	snprintf(c.state_dir, sizeof c.state_dir, "%s", dir);
	snprintf(c.port_kind, sizeof c.port_kind, "member");
	snprintf(c.listen, sizeof c.listen, "0");
	snprintf(c.switchboard, sizeof c.switchboard, "%s", sb);
	snprintf(c.token, sizeof c.token, "%s", tok);
	snprintf(c.name, sizeof c.name, "%s", nm);
	c.listed = listed ? 1 : 0;
	c.embedded = 1;
	c.leaf = 1;				/* a phone keeps sessions with the numbers in its list and no others (LW-3): no mesh, and a ring from anyone else is refused */
	c.answer = 1;				/* there is no script on a phone: the engine lets in the members of its own line and refuses everyone else */
	c.no_nat_pmp = 1;			/* a phone has no router of its own to ask, and asking costs battery */
	c.mtu = 1400;
	c.keepalive = keepalive >= 10 && keepalive <= 120 ? keepalive : 25;	/* what this end sends when idle: the battery saver says 55 */
	c.rekey_after = 300;			/* a handshake is two packets and a key exchange: every five minutes is enough for a phone, and the desktop's two is not */
	c.maxpeers = 16;
	if (!(g_node = lw_node_open(&c, err, sizeof err)) || pthread_create(&g_thr, 0, serve, 0)) {
		if (g_node && !*err)
			snprintf(err, sizeof err, "cannot start the engine's thread");
		lw_node_release(g_node);
		g_node = 0;
		r = (*env)->NewStringUTF(env, err);
		goto done;
	}
	r = (*env)->NewStringUTF(env, "");
done:
	crypto_wipe(c.seed_mem, sizeof c.seed_mem);		/* the node has its own copy, or none was made */
	(*env)->ReleaseStringUTFChars(env, jdir, dir);
	(*env)->ReleaseStringUTFChars(env, jsb, sb);
	(*env)->ReleaseStringUTFChars(env, jtoken, tok);
	(*env)->ReleaseStringUTFChars(env, jname, nm);
	return r;
}

/* The tunnel the app has built (or -1: it has none). It returns when the engine has taken it, after which the app may close the one it had. */
JNIEXPORT jboolean JNICALL Java_org_longwire_Engine_attach(JNIEnv *env, jclass cls, jint fd)
{
	(void)env;
	(void)cls;
	return g_node && leaf_attach(g_node, fd) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_org_longwire_Engine_stop(JNIEnv *env, jclass cls)
{
	(void)env;
	(void)cls;
	if (!g_node)
		return;
	g_node->stop = 1;
	pthread_join(g_thr, 0);
	lw_node_release(g_node);
	g_node = 0;
}

/* The seed as its 33 words, for the person to write down (docs/CLI.md `key export`): "" if the seed is not 32 bytes. */
JNIEXPORT jstring JNICALL Java_org_longwire_Engine_words(JNIEnv *env, jclass cls, jbyteArray jseed)
{
	uint8_t seed[32];
	char words[600];
	jstring r;

	(void)cls;
	if (!jseed || (*env)->GetArrayLength(env, jseed) != 32)
		return (*env)->NewStringUTF(env, "");
	(*env)->GetByteArrayRegion(env, jseed, 0, 32, (jbyte *)seed);
	lw_seed_words(words, sizeof words, seed);
	r = (*env)->NewStringUTF(env, words);
	crypto_wipe(words, sizeof words);
	crypto_wipe(seed, sizeof seed);
	return r;
}

/* The seed a person's 33 words give, or null if the words are not from the list or the checksum is wrong. */
JNIEXPORT jbyteArray JNICALL Java_org_longwire_Engine_parse(JNIEnv *env, jclass cls, jstring jwords)
{
	const char *w;
	uint8_t seed[32];
	jbyteArray r = 0;

	(void)cls;
	if (!jwords || !(w = (*env)->GetStringUTFChars(env, jwords, 0)))
		return 0;
	if (!lw_seed_parse(w, seed)) {
		r = (*env)->NewByteArray(env, 32);
		if (r)
			(*env)->SetByteArrayRegion(env, r, 0, 32, (jbyte *)seed);
	}
	crypto_wipe(seed, sizeof seed);
	(*env)->ReleaseStringUTFChars(env, jwords, w);
	return r;
}
