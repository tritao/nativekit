@block palette
float shade_scale() {
#ifdef PREVIEW
    return 0.5;
#else
    return 1.0;
#endif
}
@end
