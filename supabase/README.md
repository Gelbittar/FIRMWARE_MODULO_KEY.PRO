# Backend seguro KEY.PRO

## Qué contiene

- `migrations/202609220001_secure_command_pipeline.sql`: autorización por módulo, provisión de dispositivo, órdenes caducables y auditoría.
- `functions/issue-command`: recibe la petición autenticada de una app, aplica la jerarquía y firma una orden Ed25519.
- `functions/device-commands`: entrega al ESP32 exclusivamente órdenes firmadas y vigentes.
- `functions/device-result`: recibe el resultado del ESP32 y lo audita.

Las aplicaciones no conocen credenciales MQTT ni secretos de módulo. El ESP32 valida cada firma usando la clave pública integrada en el firmware. La siguiente fase sustituirá el cliente MQTT del firmware por estos endpoints HTTPS.

## Despliegue inicial

1. En Supabase SQL Editor ejecuta la migración completa.
2. Genera un par Ed25519 en un equipo local:

   ```bash
   node tools/generate-command-signing-key.mjs
   ```

3. Guarda **solo** el objeto `privateJwk` como secreto `COMMAND_SIGNING_PRIVATE_JWK` de las Edge Functions. No lo subas a Git, no lo pongas en la app y no lo copies al ESP32.
4. Despliega las tres Edge Functions con la CLI de Supabase desde un entorno autenticado:

   ```bash
   supabase functions deploy issue-command
   supabase functions deploy device-commands --no-verify-jwt
   supabase functions deploy device-result --no-verify-jwt
   ```

   `device-commands` y `device-result` validan una credencial distinta por ESP32 en su propio código; por eso no usan JWT de usuario.
5. Conserva `publicJwk`: será compilado como la clave pública de verificación del firmware, nunca como secreto.

## Contrato de firma v1

La firma Ed25519 cubre exactamente esta cadena UTF-8, separada por saltos de línea:

```text
keypro-command-v1
<command_id>
<module_id>
<action>
<nonce>
<expires_at_unix>
<payload_json_base64url>
```

El firmware debe verificar la firma antes de decodificar y ejecutar `payload_json_base64url`; debe guardar nonces ejecutados y rechazar todo comando caducado, repetido o con firma no válida. Si recibe de nuevo un nonce ya ejecutado, no debe repetir el efecto: debe reenviar el resultado almacenado al endpoint `device-result`.
