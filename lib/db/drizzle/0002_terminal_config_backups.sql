CREATE TABLE "terminal_config_backups" (
  "id" serial PRIMARY KEY NOT NULL,
  "terminal_id" text NOT NULL,
  "api_key_id" integer,
  "schema_version" integer NOT NULL,
  "firmware_version" text,
  "ciphertext" text NOT NULL,
  "iv" text NOT NULL,
  "auth_tag" text NOT NULL,
  "checksum" text NOT NULL,
  "created_at" timestamp with time zone DEFAULT now() NOT NULL,
  "updated_at" timestamp with time zone DEFAULT now() NOT NULL,
  "last_restored_at" timestamp with time zone,
  "revoked_at" timestamp with time zone
);--> statement-breakpoint
CREATE TABLE "terminal_restore_authorizations" (
  "id" serial PRIMARY KEY NOT NULL,
  "backup_id" integer NOT NULL,
  "target_terminal_id" text NOT NULL,
  "target_api_key_id" integer NOT NULL,
  "code_hash" text NOT NULL,
  "expires_at" timestamp with time zone NOT NULL,
  "created_by" integer,
  "created_at" timestamp with time zone DEFAULT now() NOT NULL,
  "used_at" timestamp with time zone,
  "revoked_at" timestamp with time zone,
  CONSTRAINT "terminal_restore_authorizations_code_hash_unique" UNIQUE("code_hash")
);--> statement-breakpoint
ALTER TABLE "terminal_config_backups" ADD CONSTRAINT "terminal_config_backups_api_key_id_api_keys_id_fk" FOREIGN KEY ("api_key_id") REFERENCES "public"."api_keys"("id") ON DELETE set null ON UPDATE no action;--> statement-breakpoint
ALTER TABLE "terminal_restore_authorizations" ADD CONSTRAINT "terminal_restore_authorizations_backup_id_terminal_config_backups_id_fk" FOREIGN KEY ("backup_id") REFERENCES "public"."terminal_config_backups"("id") ON DELETE cascade ON UPDATE no action;--> statement-breakpoint
ALTER TABLE "terminal_restore_authorizations" ADD CONSTRAINT "terminal_restore_authorizations_target_api_key_id_api_keys_id_fk" FOREIGN KEY ("target_api_key_id") REFERENCES "public"."api_keys"("id") ON DELETE cascade ON UPDATE no action;--> statement-breakpoint
ALTER TABLE "terminal_restore_authorizations" ADD CONSTRAINT "terminal_restore_authorizations_created_by_spieler_id_fk" FOREIGN KEY ("created_by") REFERENCES "public"."spieler"("id") ON DELETE set null ON UPDATE no action;--> statement-breakpoint
CREATE INDEX "terminal_config_backups_terminal_idx" ON "terminal_config_backups" USING btree ("terminal_id","updated_at");--> statement-breakpoint
CREATE INDEX "terminal_restore_auth_code_idx" ON "terminal_restore_authorizations" USING btree ("code_hash");--> statement-breakpoint
CREATE INDEX "terminal_restore_auth_expiry_idx" ON "terminal_restore_authorizations" USING btree ("expires_at");