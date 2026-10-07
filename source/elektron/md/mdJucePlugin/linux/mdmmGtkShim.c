/* The editors' GTK 3 shim on Linux (doc/release/LINUX.md, mdmmLinuxWebView.cmake).
 *
 * JUCE 7 opens "libgtk-3.so" with dlopen: the development symlink, which only libgtk-3-dev installs. This
 * library has that name and nothing in it but a dependency on the runtime library, libgtk-3.so.0; dlsym on it
 * searches its dependencies, so JUCE finds gtk_* and g_* there. It lies beside the standalone and the VST3
 * module, found through their $ORIGIN run path.
 */
void mdmmGtkShimMarker(void);
void mdmmGtkShimMarker(void)
{
}
