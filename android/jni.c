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
JNIEXPORT jstring JNICALL Java_org_longwire_Engine_start(JNIEnv *env, jclass cls, jstring jdir, jstring jsb, jstring jtoken, jstring jname, jboolean listed)
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
	c.answer = 1;				/* there is no script on a phone: the engine lets in the members of its own line and refuses everyone else */
	c.no_nat_pmp = 1;			/* a phone has no router of its own to ask, and asking costs battery */
	c.mtu = 1400;
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
	(*env)->ReleaseStringUTFChars(env, jdir, dir);
	(*env)->ReleaseStringUTFChars(env, jsb, sb);
	(*env)->ReleaseStringUTFChars(env, jtoken, tok);
	(*env)->ReleaseStringUTFChars(env, jname, nm);
	return r;
}

/* The tunnel the app has built (or -1: it has none). It returns when the engine has taken it, after which the app may close the one it had. */
JNIEXPORT void JNICALL Java_org_longwire_Engine_attach(JNIEnv *env, jclass cls, jint fd)
{
	(void)env;
	(void)cls;
	if (g_node)
		leaf_attach(g_node, fd);
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
