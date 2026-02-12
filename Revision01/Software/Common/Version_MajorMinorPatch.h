//
#if 0

/* see: https://semver.org/ */
/* database versions. changing this will cause a new dbi to be cloned programmatically. */

#define Version__major() 1  /* increment on incompatible change */
#define Version__minor() 0  /* increment on compatible feature */
#define Version__patch() 0  /* increment on compatible patch */
#define Version_() { Version__major(), Version__minor(), Version__patch() }
#endif
