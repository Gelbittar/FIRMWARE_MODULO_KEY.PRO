#!/usr/bin/env node
// Run locally; never save this output in the repository or in an APK.
// The private JWK goes only to the Supabase secret COMMAND_SIGNING_PRIVATE_JWK.
const pair = await crypto.subtle.generateKey({ name: 'Ed25519' }, true, ['sign', 'verify']);
const privateJwk = await crypto.subtle.exportKey('jwk', pair.privateKey);
const publicJwk = await crypto.subtle.exportKey('jwk', pair.publicKey);
console.log(JSON.stringify({ privateJwk, publicJwk }, null, 2));
