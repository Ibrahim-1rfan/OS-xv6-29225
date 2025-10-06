#include "kernel/types.h"
#include "user/user.h"

int main() {
    char *p;
    
    // Allocate 16MB page by page
    for(int i = 0; i < 4096; i++) {
        p = sbrk(4096);
        if(p == (char*)-1) break;
        
        // Search this page
        for(int j = 0; j < 4096; j++) {
            char *s = p + j;
            int len = 0;
            
            // Check string
            while(len < 50 && s[len] != '\0' && 
                  ((s[len] >= 'a' && s[len] <= 'z') || 
                   (s[len] >= 'A' && s[len] <= 'Z') || 
                   (s[len] >= '0' && s[len] <= '9'))) {
                len++;
            }
            
            if(len > 0 && s[len] == '\0') {
                printf("%s\n", s);
                exit(0);
            }
        }
    }
    
    exit(1);
}
