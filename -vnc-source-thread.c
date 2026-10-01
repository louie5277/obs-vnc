warning: in the working copy of 'src/obs-vnc-source-thread.c', LF will be replaced by CRLF the next time Git touches it
[1mdiff --git a/src/obs-vnc-source-thread.c b/src/obs-vnc-source-thread.c[m
[1mindex 1bf1230..75dc876 100644[m
[1m--- a/src/obs-vnc-source-thread.c[m
[1m+++ b/src/obs-vnc-source-thread.c[m
[36m@@ -260,8 +260,8 @@[m [mstatic void enable_tcp_keepalive(rfbClient *client)[m
 	keepalive.keepaliveinterval = 5000;[m
 [m
 	DWORD bytes_returned = 0;[m
[31m-	if (WSAIoctl(sock, SIO_KEEPALIVE_VALS, &keepalive, sizeof(keepalive), NULL, 0, &bytes_returned,[m
[31m-		     NULL, NULL) != 0) {[m
[32m+[m	[32mif (WSAIoctl(sock, SIO_KEEPALIVE_VALS, &keepalive, sizeof(keepalive), NULL, 0, &bytes_returned, NULL, NULL) !=[m
[32m+[m	[32m    0) {[m
 		blog(LOG_WARNING, "obs-vnc: failed to configure TCP keepalive (WSA error %d)", WSAGetLastError());[m
 	}[m
 }[m
[36m@@ -606,7 +606,7 @@[m [mstatic inline int vkey_native_to_rfb(int vkey, int modifiers)[m
 		case 0x31: return ' ';[m
 		case 0x32: return '`'; // SDL_SCANCODE_GRAVE;[m
 	}[m
[31m-		/* clang-format on */[m
[32m+[m	[32m/* clang-format on */[m
 #endif // __APPLE__[m
 [m
 	return 0;[m
[36m@@ -686,8 +686,7 @@[m [mstatic inline void rfbc_interact_one(rfbClient *client, struct vncsrc_keymouse_s[m
 			/* clang-format off */[m
 			{INTERACT_CONTROL_KEY, XK_Control_L},[m
 			{INTERACT_SHIFT_KEY, XK_Shift_L},[m
[31m-			{0, 0}[m
[31m-			/* clang-format on */[m
[32m+[m			[32m{0, 0} /* clang-format on */[m
 		};[m
 		if (key)[m
 			for (int i = 0; mm[i][0]; i++) {[m
