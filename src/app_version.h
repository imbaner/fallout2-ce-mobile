#ifndef FALLOUT_APP_VERSION_H_
#define FALLOUT_APP_VERSION_H_

namespace fallout {

// CE: The app's version ("1.3.0-mobile.9") and build (the commit; on the web
// the build's id), as the About section and problem reports show them. The
// web page gives them (its build-info.json): the engine's files stay the same
// from one build to the next while the engine's code does.
const char* appVersion();
const char* appBuild();

} // namespace fallout

#endif /* FALLOUT_APP_VERSION_H_ */
